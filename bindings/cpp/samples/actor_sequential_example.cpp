/* SPDX-License-Identifier: MPL-2.0 */
/*
 * 자립형 가이드 예제: STREAM이 relay한 메시지를 Actor가 순서대로 처리.
 * Actor는 생성 시 Entry Spot(로비)에 있다가 join으로 개별 room(user Spot)으로
 * 옮겨 간다. 메시지는 STREAM session에 actor를 bind하고 packet을 relay해야만
 * 도달하며, room의 dispatch context에서 들어온 순서대로 처리된다.
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

// --8<-- [start:doc]
int main ()
{
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    zlink::service::spot_t room = node.create_spot ();
    // 생성 직후 actor는 Entry Spot(로비)에 위치한다.
    zlink::service::actor_t player = node.create_actor ("player");
    zlink::stream_socket_t stream (ctx);

    stream.attach_actor_gateway (node);
    zlink::routing_id_t session =
      zlink::routing_id_t::from (std::string ("player-session"));
    // STREAM session에 actor를 bind한다 (이후 relay가 이 actor로 간다).
    (void) stream.bind_actor (session, player.ref ()).submit_async ().get ();

    // dispatch 핸들러: join을 수락하고, STREAM이 relay한 메시지를 모은다.
    std::mutex mutex;
    std::vector<std::string> processed;
    room.set_dispatch_handler (
      [&] (zlink::service::spot_t &s, const zlink::spot_dispatch_info_t &info) {
          if (info.event == zlink::spot_dispatch_event_t::actor_join_readable) {
              auto request = s.recv_actor_join (zlink::recv_flags_t::dontwait);
              if (!request.has_value ())
                  return;
              zlink::message_t reply = zlink::message_t::from ("accepted");
              s.reply_actor_join (*request, 0).message (reply).submit ();
          } else if (info.event == zlink::spot_dispatch_event_t::actor_readable) {
              while (auto part = node.recv_actor_part (
                       *info.actor, zlink::recv_flags_t::dontwait)) {
                  std::lock_guard<std::mutex> lock (mutex);
                  processed.push_back (part->part.to_string ());
              }
          }
      });

    // join으로 Entry Spot에서 room(user Spot)으로 이동한다.
    std::promise<zlink::request_result_t> joined;
    auto joined_future = joined.get_future ();
    zlink::message_t join_msg = zlink::message_t::from ("enter-room");
    player.join (room).message (join_msg).timeout (std::chrono::seconds (1)).submit (
      [&joined] (const zlink::actor_join_result_t &result,
                 std::vector<zlink::message_t>) { joined.set_value (result.result); });
    (void) joined_future.get ();

    // STREAM이 플레이어 입력을 연달아 relay한다 — actor는 순서대로 처리한다.
    const char *commands[] = {"move", "attack", "loot"};
    for (const char *command : commands) {
        zlink::message_t m = zlink::message_t::from (command);
        stream.send_bound_actor (session, "player").message (m).submit ();
    }

    auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (2);
    while (std::chrono::steady_clock::now () < deadline) {
        {
            std::lock_guard<std::mutex> lock (mutex);
            if (processed.size () >= 3)
                break;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    {
        std::lock_guard<std::mutex> lock (mutex);
        assert (processed.size () == 3 && processed[0] == "move"
                && processed[1] == "attack" && processed[2] == "loot");
    }

    (void) player.leave (room).submit_async ().get ();
    player.close ();
    std::printf (
      "[actor/sequential] processed in order: move -> attack -> loot\n");
    return 0;
}
// --8<-- [end:doc]
