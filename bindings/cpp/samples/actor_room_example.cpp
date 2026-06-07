/* SPDX-License-Identifier: MPL-2.0 */
/*
 * 자립형 가이드 예제: 한 방(Spot)의 두 플레이어(Actor).
 * 서버가 각 플레이어에게 id로 주소 지정해 메시지를 보내면, 그 Actor만 받는다.
 */

#include <boost/asio.hpp>
#include <zlink.hpp>

#include <cassert>
#include <chrono>
#include <cstdio>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

int main ()
{
    // --8<-- [start:doc]
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    zlink::service::spot_t room = node.create_spot ();
    zlink::service::actor_t player1 = node.create_actor ("player-1");
    zlink::service::actor_t player2 = node.create_actor ("player-2");
    zlink::stream_socket_t stream (ctx);

    stream.attach_actor_gateway (node);
    zlink::routing_id_t session = zlink::routing_id_t::from (std::string ("game-room-session"));
    (void) stream.bind_actor (session, player1.ref ()).submit_async ().get ();
    (void) stream.bind_actor (session, player2.ref ()).submit_async ().get ();

    // dispatch 핸들러: join 요청을 수락하고, 도착한 메시지를 모은다.
    std::mutex mutex;
    std::vector<std::string> received;
    room.set_dispatch_handler ([&] (zlink::service::spot_t &s,
                                    const zlink::spot_dispatch_info_t &info) {
        if (info.event == zlink::spot_dispatch_event_t::actor_join_readable) {
            auto request = s.recv_actor_join (zlink::recv_flags_t::dontwait);
            if (!request.has_value ())
                return;
            zlink::message_t reply = zlink::message_t::from ("accepted");
            s.reply_actor_join (*request, 0).message (reply).submit ();
        } else if (info.event == zlink::spot_dispatch_event_t::actor_readable) {
            while (auto part = node.recv_actor_part (*info.actor, zlink::recv_flags_t::dontwait)) {
                std::lock_guard<std::mutex> lock (mutex);
                received.push_back (part->part.to_string ());
            }
        }
    });

    auto join = [&] (zlink::service::actor_t &actor) {
        std::promise<zlink::request_result_t> done;
        auto future = done.get_future ();
        zlink::message_t m = zlink::message_t::from ("enter-room");
        actor.join (room)
          .message (m)
          .timeout (std::chrono::seconds (1))
          .submit ([&done] (const zlink::actor_join_result_t &result,
                            std::vector<zlink::message_t>) { done.set_value (result.result); });
        (void) future.get ();
    };
    // 보낸 직후 도착을 기다리므로, 그 메시지는 방금 주소 지정한 플레이어 것이다.
    auto send_and_wait = [&] (const char *actor_id, const char *text, size_t want) {
        zlink::message_t m = zlink::message_t::from (text);
        stream.send_bound_actor (session, actor_id).message (m).submit ();
        auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (2);
        while (std::chrono::steady_clock::now () < deadline) {
            {
                std::lock_guard<std::mutex> lock (mutex);
                if (received.size () >= want)
                    return;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
        }
        assert (false && "message not delivered");
    };
    auto leave = [&] (zlink::service::actor_t &actor) {
        (void) actor.leave (room).submit_async ().get ();
    };

    join (player1);
    join (player2);

    // 서버가 각 플레이어에게 자기 앞으로 온 메시지를 보낸다.
    send_and_wait ("player-1", "your-turn", 1);
    send_and_wait ("player-2", "wait", 2);

    {
        std::lock_guard<std::mutex> lock (mutex);
        assert (received.size () == 2 && received[0] == "your-turn" && received[1] == "wait");
    }

    leave (player1);
    leave (player2);
    player1.close ();
    player2.close ();
    std::printf ("[actor/room] player-1: \"your-turn\", player-2: \"wait\"\n");
    return 0;
    // --8<-- [end:doc]
}
