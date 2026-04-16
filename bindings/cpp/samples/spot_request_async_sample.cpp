/* SPDX-License-Identifier: MPL-2.0 */

#include "sample_common.hpp"

#include <thread>

int main ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t requester_node (ctx);
    zlink::service::spot_node_t responder_node (ctx);
    zlink::service::spot_t requester = requester_node.create_spot ();
    zlink::service::spot_t responder_spot = responder_node.create_spot ();
    assert (requester_node.valid ());
    assert (responder_node.valid ());
    assert (requester.valid ());
    assert (responder_spot.valid ());

    const std::string endpoint = detail::unique_tcp ("spot-request-async");
    responder_node.bind (endpoint);
    requester_node.connect_peer (endpoint);

    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (5);
    while (std::chrono::steady_clock::now () < deadline) {
        bool service_ready = false;
        const auto peers = requester_node.peers_snapshot ();
        for (const auto &peer : peers) {
            if (peer.service_name == detail::k_spot_service
                && peer.state == zlink::spot_peer_state::connected) {
                service_ready = true;
                break;
            }
        }
        if (requester_node.status_snapshot ().connected_peer_count > 0
            && service_ready) {
            break;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    assert (requester_node.status_snapshot ().connected_peer_count > 0);

    std::thread responder ([&responder_spot] {
        const zlink::received_t received = responder_spot.recv_routed ();
        assert (received.parts ().size () == 1);
        assert (received.routing_id ().has_value ());
        assert (received.spot_rid ().has_value ());
        assert (received.request_seq ().has_value ());
        assert (received.parts ()[0].to_string () == "spot-ping");
        zlink::message_t reply = detail::make_message ("spot-pong");
        received.reply (reply);
    });

    zlink::message_t request = detail::make_message ("spot-ping");
    std::vector<zlink::message_t> reply_parts =
      requester.request_to_spot (responder_node.routing_id (),
                                 responder_spot.routing_id (),
                                 std::move (request),
                                 std::chrono::milliseconds (5000))
        .get ();
    assert (reply_parts.size () == 1);
    const std::string reply = reply_parts[0].to_string ();
    assert (reply == "spot-pong");
    responder.join ();
    std::printf (
      "[spot/request/async] request: \"spot-ping\" -> reply: \"%s\"\n",
      reply.c_str ());
    return 0;
}
