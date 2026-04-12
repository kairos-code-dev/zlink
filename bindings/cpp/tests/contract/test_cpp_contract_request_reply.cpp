/* SPDX-License-Identifier: MPL-2.0 */

#include "support.hpp"

#include <chrono>
#include <cstdlib>

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
    zlink::request_dealer_t dealer (dealer_socket);
    zlink::request_router_t router (router_socket);
    zlink::monitor_handle_t router_monitor = router_socket.monitor_handle ();
    zlink::monitor_handle_t dealer_monitor = dealer_socket.monitor_handle ();

    const std::string endpoint = zlink_cpp_contract::unique_inproc ("rr-cpp");
    assert (router_socket.bind (endpoint) == 0);
    assert (dealer_socket.connect (endpoint) == 0);
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      router_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));
    assert (zlink_cpp_contract::wait_for_socket_monitor_event (
      dealer_monitor,
      static_cast<uint64_t> (zlink::monitor_event::connection_ready), 2000));

    router.on_receive ([&router] (zlink::received_t request) {
        assert (request.parts.size () == 1);
        assert (request.has_request_seq);
        assert (request.request_seq != 0u);

        zlink::message_t reply = make_request_message ("reply:ok");
        router.reply (request.routing_id, request.request_seq, std::move (reply));
    });

    zlink::async_result_t<zlink::received_t> future =
      dealer.request (make_request_message ("request:ping"),
                      std::chrono::milliseconds (5000));
    const zlink::received_t reply = future.get ();
    assert (reply.parts.size () == 1);
    assert (reply.parts[0].to_string () == "reply:ok");
}

void test_request_router_preserves_data_recv_surface ()
{
    zlink::context_t ctx;
    zlink::dealer_socket_t dealer_socket (ctx);
    zlink::router_socket_t router_socket (ctx);
    zlink::request_router_t router (router_socket);
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

    zlink::message_t data = make_request_message ("plain-data");
    dealer_socket.send (data);

    const zlink::received_t received = router.recv ();
    assert (received.parts.size () == 1);
    assert (received.parts[0].to_string () == "plain-data");
    assert (!received.has_request_seq);
}

} // namespace

int main ()
{
    test_request_dealer_router_roundtrip ();
    test_request_router_preserves_data_recv_surface ();
    std::quick_exit (0);
}
