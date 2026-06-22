/* SPDX-License-Identifier: MPL-2.0 */
/*
 * 자립형 가이드 예제: SPOT → 채널(DEALER→ROUTER) 요청.
 * 게임룸(Spot)이 API 서버(채널 서비스)에 outgame 데이터를 요청한다.
 */

#include "sample_common.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

int main ()
{
    // --8<-- [start:doc]
    zlink::context_t ctx;
    zlink::service::spot_node_t room_node (ctx);
    zlink::service::spot_t room = room_node.create_spot ();
    zlink::dealer_socket_t room_dealer (ctx);
    zlink::router_socket_t api_router (ctx);
    zlink::service::spot_route_bridge_t bridge = room_node.create_route_bridge ();

    const std::string channel = "api";
    const std::string endpoint = detail::unique_tcp ("spot-channel");
    room_dealer.set_routing_id (zlink::routing_id_t::from (
      reinterpret_cast<const uint8_t *> ("room-channel-client"), 19));
    api_router.bind (endpoint);
    room_dealer.connect (endpoint);
    std::this_thread::sleep_for (std::chrono::milliseconds (50));
    // "api" 채널 호출을 이 DEALER로 내보내도록 bridge에 등록한다.
    bridge.attach_dealer_channel (channel, room_dealer);

    // 게임룸이 API 채널로 outgame 요청을 보낸다.
    zlink::message_t request = zlink::message_t::from ("get-profile");
    std::vector<zlink::message_t> parts;
    parts.push_back (std::move (request));
    zlink::async_result_t<std::vector<zlink::message_t>> pending =
      bridge.request (channel, room.routing_id (), parts, std::chrono::seconds (5));

    // API 서버(ROUTER)는 요청을 받아 응답한다.
    zlink::received_t received;
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (5);
    bool served = false;
    while (std::chrono::steady_clock::now () < deadline) {
        if (api_router.recv (received, zlink::recv_flags_t::dontwait)
            == static_cast<int> (zlink::recv_result_t::ok)) {
            zlink::message_t reply_message = zlink::message_t::from ("profile:level-7");
            received.reply ().message (reply_message).submit ();
            served = true;
            break;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    if (!served) {
        std::fprintf (stderr, "timed out waiting for bridge relay packet\n");
        return 2;
    }

    if (pending.wait_for (std::chrono::seconds (5)) != std::future_status::ready) {
        std::fprintf (stderr, "timed out waiting for bridge reply\n");
        return 3;
    }
    std::vector<zlink::message_t> reply = pending.get ();

    std::printf ("[spot/channel] request \"get-profile\" -> reply \"%s\"\n",
                 reply.empty () ? "" : reply[0].to_string ().c_str ());
    return 0;
    // --8<-- [end:doc]
}
