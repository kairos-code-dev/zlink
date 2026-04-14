/* SPDX-License-Identifier: MPL-2.0 */

#include "support.hpp"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <thread>

namespace {

zlink::message_t make_request_message (const std::string &text_)
{
    return zlink_cpp_contract::make_message (text_);
}

void test_request_dealer_router_roundtrip ()
{
    zlink::context_t ctx;
    zlink::dealer_socket_t dealer_socket (ctx);
    zlink::router_socket_t router_socket (ctx);
    zlink::monitor_handle_t router_monitor = router_socket.monitor_handle ();
    zlink::monitor_handle_t dealer_monitor = dealer_socket.monitor_handle ();
    const std::string routing_id_text = "request-reply-client";
    zlink::routing_id_t routing_id = zlink::routing_id_t::from_bytes (
      reinterpret_cast<const uint8_t *> (routing_id_text.data ()),
      routing_id_text.size ());

    const std::string endpoint = zlink_cpp_contract::unique_inproc ("rr-cpp");
    dealer_socket.set_routing_id (routing_id);
    assert (router_socket.bind (endpoint) == 0);
    assert (dealer_socket.connect (endpoint) == 0);
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      router_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      dealer_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));
    std::this_thread::sleep_for (std::chrono::milliseconds (50));

    zlink::message_t warmup = make_request_message ("warmup");
    dealer_socket.send (warmup);
    std::this_thread::sleep_for (std::chrono::milliseconds (50));
    const zlink::received_t warmup_received = router_socket.recv ();
    assert (warmup_received.parts ().size () == 1);
    assert (warmup_received.parts ()[0].to_string () == "warmup");

    zlink::message_t request = make_request_message ("request:ping");
    std::future<void> router_done = std::async (
      std::launch::async, [&router_socket] () {
          const zlink::received_t request = router_socket.recv ();
          assert (request.parts ().size () == 1);
          assert (request.request_seq ().has_value ());
          assert (*request.request_seq () != 0u);

          zlink::message_t reply = make_request_message ("reply:ok");
          request.reply (reply);
      });

    zlink::async_result_t<std::vector<zlink::message_t>> future =
      dealer_socket.request (request, std::chrono::milliseconds (5000));
    const std::vector<zlink::message_t> reply = future.get ();
    assert (reply.size () == 1);
    assert (reply[0].to_string () == "reply:ok");
    router_done.get ();
}

void test_request_router_preserves_data_recv_surface ()
{
    zlink::context_t ctx;
    zlink::dealer_socket_t dealer_socket (ctx);
    zlink::router_socket_t router_socket (ctx);
    zlink::monitor_handle_t router_monitor = router_socket.monitor_handle ();
    zlink::monitor_handle_t dealer_monitor = dealer_socket.monitor_handle ();

    const std::string endpoint =
      zlink_cpp_contract::unique_inproc ("rr-cpp-data");
    assert (router_socket.bind (endpoint) == 0);
    assert (dealer_socket.connect (endpoint) == 0);
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      router_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      dealer_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));
    std::this_thread::sleep_for (std::chrono::milliseconds (50));

    zlink::message_t data = make_request_message ("plain-data");
    dealer_socket.send (data);

    const zlink::received_t received = router_socket.recv ();
    assert (received.parts ().size () == 1);
    assert (received.parts ()[0].to_string () == "plain-data");
    assert (!received.request_seq ().has_value ());
}

void test_received_reply_rejects_non_none_flags ()
{
    zlink::context_t ctx;
    zlink::dealer_socket_t dealer_socket (ctx);
    zlink::router_socket_t router_socket (ctx);
    zlink::monitor_handle_t router_monitor = router_socket.monitor_handle ();
    zlink::monitor_handle_t dealer_monitor = dealer_socket.monitor_handle ();
    const std::string routing_id_text = "request-reply-flags-client";
    zlink::routing_id_t routing_id = zlink::routing_id_t::from_bytes (
      reinterpret_cast<const uint8_t *> (routing_id_text.data ()),
      routing_id_text.size ());

    const std::string endpoint =
      zlink_cpp_contract::unique_inproc ("rr-cpp-reply-flags");
    dealer_socket.set_routing_id (routing_id);
    assert (router_socket.bind (endpoint) == 0);
    assert (dealer_socket.connect (endpoint) == 0);
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      router_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      dealer_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));
    std::this_thread::sleep_for (std::chrono::milliseconds (50));

    zlink::message_t request = make_request_message ("request:flags");
    std::future<void> router_done = std::async (
      std::launch::async, [&router_socket] () {
          const zlink::received_t received = router_socket.recv ();
          assert (received.request_seq ().has_value ());

          zlink::message_t rejected = make_request_message ("reply:rejected");
          try {
              received.reply (rejected, zlink::send_flags_t::dontwait);
              assert (false && "reply flags must be rejected");
          } catch (const zlink::submit_error_t &error) {
              assert (error.result () == zlink::submit_result_t::not_supported);
              assert (error.internal_errno () == ENOTSUP);
          }

          zlink::message_t accepted = make_request_message ("reply:ok");
          received.reply (accepted);
      });

    zlink::async_result_t<std::vector<zlink::message_t>> future =
      dealer_socket.request (request, std::chrono::milliseconds (5000));
    const std::vector<zlink::message_t> reply = future.get ();
    assert (reply.size () == 1);
    assert (reply[0].to_string () == "reply:ok");
    router_done.get ();
}

} // namespace

int main ()
{
    test_request_dealer_router_roundtrip ();
    test_request_router_preserves_data_recv_surface ();
    test_received_reply_rejects_non_none_flags ();
    std::quick_exit (0);
}
