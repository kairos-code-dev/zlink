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

static void wait_connected (zlink::socket_monitor_t &monitor)
{
    auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (5);
    while (std::chrono::steady_clock::now () < deadline) {
        auto event = monitor.recv (zlink::recv_flags_t::dontwait);
        if (event && event->event == zlink::monitor_event::connection_ready)
            return;
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
}

int main ()
{
    // --8<-- [start:doc]
    zlink::context_t ctx;
    zlink::service::spot_node_t room_node (ctx);
    zlink::service::spot_t room = room_node.create_spot ();
    zlink::dealer_socket_t room_dealer (ctx);
    zlink::router_socket_t api_router (ctx);

    const std::string channel = "api";
    const std::string endpoint = detail::unique_tcp ("spot-channel");
    zlink::socket_monitor_t router_monitor = api_router.monitor_open ();
    zlink::socket_monitor_t dealer_monitor = room_dealer.monitor_open ();
    api_router.bind (endpoint);
    room_dealer.connect (endpoint);
    // DEALER↔ROUTER 연결이 맺어질 때까지 기다린다.
    wait_connected (router_monitor);
    wait_connected (dealer_monitor);
    // "api" 채널 호출을 이 DEALER로 내보내도록 노드에 등록한다.
    room_node.attach_channel_dealer_manual (channel, room_dealer);

    // API 서버(ROUTER)는 별도 스레드에서 요청을 받아 응답한다.
    std::thread server ([&] () {
        zlink::received_t received;
        if (api_router.recv (received) == static_cast<int> (zlink::recv_result_t::ok)) {
            zlink::message_t reply = zlink::message_t::from ("profile:level-7");
            received.reply ().message (reply).submit ();
        }
    });

    // 게임룸이 API 채널로 outgame 요청을 보낸다.
    zlink::message_t request = zlink::message_t::from ("get-profile");
    std::vector<zlink::message_t> reply = room.request_channel (channel)
                                            .message (request)
                                            .timeout (std::chrono::seconds (5))
                                            .async ()
                                            .get ();
    server.join ();

    std::printf ("[spot/channel] request \"get-profile\" -> reply \"%s\"\n",
                 reply.empty () ? "" : reply[0].to_string ().c_str ());
    return 0;
    // --8<-- [end:doc]
}
