/* SPDX-License-Identifier: MPL-2.0 */

#include "support.hpp"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <optional>
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
    router_socket.bind (endpoint);
    dealer_socket.connect (endpoint);
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      router_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      dealer_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));
    std::this_thread::sleep_for (std::chrono::milliseconds (50));

    zlink::message_t warmup = make_request_message ("warmup");
    assert (dealer_socket.send ().message (warmup).submit ());
    std::this_thread::sleep_for (std::chrono::milliseconds (50));
    zlink::received_t warmup_received;
    assert (router_socket.recv (warmup_received) == 0);
    assert (warmup_received.parts ().size () == 1);
    assert (warmup_received.parts ()[0].to_string () == "warmup");

    zlink::message_t request = make_request_message ("request:ping");
    std::future<void> router_done = std::async (
      std::launch::async, [&router_socket] () {
          zlink::received_t request;
          assert (router_socket.recv (request) == 0);
          assert (request.parts ().size () == 1);
          assert (request.request_seq ().has_value ());
          assert (*request.request_seq () != 0u);

          zlink::message_t reply = make_request_message ("reply:ok");
          request.reply ().message (reply).submit ();
      });

    zlink::async_result_t<std::vector<zlink::message_t>> future =
      dealer_socket.request ()
        .message (request)
        .timeout (std::chrono::milliseconds (5000))
        .submit_async ();
    const std::vector<zlink::message_t> reply = future.get ();
    assert (reply.size () == 1);
    assert (reply[0].to_string () == "reply:ok");
    router_done.get ();
}

void test_request_wait_for_zero_pumps_progress ()
{
    zlink::context_t ctx;
    zlink::dealer_socket_t dealer_socket (ctx);
    zlink::router_socket_t router_socket (ctx);
    zlink::monitor_handle_t router_monitor = router_socket.monitor_handle ();
    zlink::monitor_handle_t dealer_monitor = dealer_socket.monitor_handle ();

    const std::string endpoint =
      zlink_cpp_contract::unique_inproc ("rr-cpp-wait-zero");
    router_socket.bind (endpoint);
    dealer_socket.connect (endpoint);
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      router_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      dealer_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));
    std::this_thread::sleep_for (std::chrono::milliseconds (50));

    zlink::message_t request = make_request_message ("request:wait-zero");
    std::future<void> router_done = std::async (
      std::launch::async, [&router_socket] () {
          zlink::received_t request;
          assert (router_socket.recv (request) == 0);
          assert (request.parts ().size () == 1);
          assert (request.request_seq ().has_value ());

          zlink::message_t reply = make_request_message ("reply:wait-zero");
          request.reply ().message (reply).submit ();
      });

    zlink::async_result_t<std::vector<zlink::message_t>> future =
      dealer_socket.request ()
        .message (request)
        .timeout (std::chrono::milliseconds (5000))
        .submit_async ();

    bool ready = false;
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (2);
    while (std::chrono::steady_clock::now () < deadline) {
        if (future.wait_for (std::chrono::milliseconds (0))
            == std::future_status::ready) {
            ready = true;
            break;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    assert (ready && "wait_for(0) must pump request progress");

    const std::vector<zlink::message_t> reply = future.get ();
    assert (reply.size () == 1);
    assert (reply[0].to_string () == "reply:wait-zero");
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
    router_socket.bind (endpoint);
    dealer_socket.connect (endpoint);
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      router_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      dealer_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));
    std::this_thread::sleep_for (std::chrono::milliseconds (50));

    zlink::message_t data = make_request_message ("plain-data");
    assert (dealer_socket.send ().message (data).submit ());

    zlink::received_t received;
    assert (router_socket.recv (received) == 0);
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
    router_socket.bind (endpoint);
    dealer_socket.connect (endpoint);
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
          zlink::received_t received;
          assert (router_socket.recv (received) == 0);
          assert (received.request_seq ().has_value ());

          zlink::message_t rejected = make_request_message ("reply:rejected");
          try {
              received.reply ().message (rejected).flags (zlink::recv_flags_t::dontwait).submit ();
              assert (false && "reply flags must be rejected");
          } catch (const zlink::submit_error_t &error) {
              assert (error.result () == zlink::submit_result_t::not_supported);
              assert (error.internal_errno () == ENOTSUP);
          }

          zlink::message_t accepted = make_request_message ("reply:ok");
          received.reply ().message (accepted).submit ();
      });

    zlink::async_result_t<std::vector<zlink::message_t>> future =
      dealer_socket.request ()
        .message (request)
        .timeout (std::chrono::milliseconds (5000))
        .submit_async ();
    const std::vector<zlink::message_t> reply = future.get ();
    assert (reply.size () == 1);
    assert (reply[0].to_string () == "reply:ok");
    router_done.get ();
}

} // namespace

int main ()
{
    test_request_dealer_router_roundtrip ();
    test_request_wait_for_zero_pumps_progress ();
    test_request_router_preserves_data_recv_surface ();
    test_received_reply_rejects_non_none_flags ();
    std::quick_exit (0);
}
