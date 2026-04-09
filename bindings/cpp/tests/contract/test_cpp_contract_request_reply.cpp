/* SPDX-License-Identifier: MPL-2.0 */

#include "support.hpp"

#include <chrono>

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

    const std::string endpoint = zlink_cpp_contract::unique_inproc ("rr-cpp");
    assert (router_socket.bind (endpoint) == 0);
    assert (dealer_socket.connect (endpoint) == 0);

    router.on_receive ([&router] (zlink::received_t request) {
        assert (request.parts.size () == 1);
        uint8_t msg_type = 0;
        uint64_t correlation_id = 0;
        assert (request.parts[0].get_request_info (&msg_type, &correlation_id)
                == 0);
        assert (msg_type == 1u);

        zlink::message_t reply = make_request_message ("reply:ok");
        assert (router.try_reply (request.routing_id, correlation_id, std::move (reply))
                == zlink::send_result_t::sent);
    });

    zlink::async_result_t<zlink::received_t> future =
      dealer.request (make_request_message ("request:ping"),
                      std::chrono::milliseconds (5000));
    const zlink::received_t reply = future.get ();
    assert (reply.parts.size () == 1);
    assert (reply.parts[0].to_string () == "reply:ok");

    uint8_t msg_type = 0;
    uint64_t correlation_id = 0;
    assert (reply.parts[0].get_request_info (&msg_type, &correlation_id) == 0);
    assert (msg_type == 2u);
    assert (correlation_id != 0u);
}

void test_request_router_preserves_data_recv_surface ()
{
    zlink::context_t ctx;
    zlink::dealer_socket_t dealer_socket (ctx);
    zlink::router_socket_t router_socket (ctx);
    zlink::request_router_t router (router_socket);

    const std::string endpoint =
      zlink_cpp_contract::unique_inproc ("rr-cpp-data");
    assert (router_socket.bind (endpoint) == 0);
    assert (dealer_socket.connect (endpoint) == 0);

    zlink::message_t data = make_request_message ("plain-data");
    dealer_socket.send (data);

    const zlink::received_t received = router.recv ();
    assert (received.parts.size () == 1);
    assert (received.parts[0].to_string () == "plain-data");

    uint8_t msg_type = 255u;
    uint64_t correlation_id = 123u;
    assert (received.parts[0].get_request_info (&msg_type, &correlation_id)
            == 0);
    assert (msg_type == 0u);
    assert (correlation_id == 0u);
}

} // namespace

int main ()
{
    test_request_dealer_router_roundtrip ();
    test_request_router_preserves_data_recv_surface ();
    return 0;
}
