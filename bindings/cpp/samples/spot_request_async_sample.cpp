/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

#include <thread>

int main ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t requester_node (ctx);
    zlink::service::spot_t requester = requester_node.create_spot ();
    zlink::router_socket_t responder_router (ctx);
    zlink::dealer_socket_t requester_dealer (ctx);
    zlink::monitor_handle_t responder_monitor = responder_router.monitor_handle ();
    zlink::monitor_handle_t requester_monitor = requester_dealer.monitor_handle ();
    assert (requester_node.valid ());
    assert (requester.valid ());
    assert (responder_router.valid ());
    assert (requester_dealer.valid ());

    const std::string channel_name = "orders";
    const std::string endpoint = detail::unique_tcp ("spot-channel-request");
    responder_router.bind (endpoint);
    requester_dealer.connect (endpoint);
    assert (detail::wait_connected (responder_monitor, requester_monitor));
    requester_node.attach_channel_dealer_manual (channel_name, requester_dealer);

    std::thread responder ([&responder_router] {
        zlink::received_t received;
        assert (responder_router.recv (received) == 0);
        assert (received.parts ().size () == 1);
        assert (received.request_seq ().has_value ());
        assert (received.parts ()[0].to_string () == "spot-ping");
        zlink::message_t reply = detail::make_message ("spot-pong");
        received.reply (reply);
        received.close ();
    });

    std::vector<zlink::message_t> request_parts;
    request_parts.push_back (detail::make_message ("spot-ping"));
    zlink::async_result_t<std::vector<zlink::message_t>> reply_future =
      requester.request_channel (channel_name)
        .message (request_parts.front ())
        .timeout (std::chrono::milliseconds (5000))
        .submit_async ();
    std::vector<zlink::message_t> reply_parts = reply_future.get ();
    assert (reply_parts.size () == 1);
    const std::string reply = reply_parts[0].to_string ();
    assert (reply == "spot-pong");
    responder.join ();
    std::printf (
      "[spot/request/async] request: \"spot-ping\" -> reply: \"%s\"\n",
      reply.c_str ());
    return 0;
}
