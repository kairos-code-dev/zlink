/* SPDX-License-Identifier: MPL-2.0 */
/*
 * 자립형 가이드 예제: SPOT Actor의 재접속 이전성(single-player queue).
 * 한 파일 안에 전체 흐름이 들어 있다 — 별도 헬퍼 없이 빌드·실행된다.
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
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    zlink::service::spot_t spot = node.create_spot ();
    zlink::service::actor_t actor = node.create_actor ("single-player");
    zlink::stream_socket_t stream (ctx);

    // 스트림 게이트웨이에 actor를 세션으로 바인딩한다. 실제 서버에서 session은
    // 게이트웨이로 접속한 클라이언트의 라우팅 ID다 — 여기선 고정값으로 만든다.
    zlink::routing_id_t session = zlink::routing_id_t::from (std::string ("single-player-session"));
    (void) stream.bind_actor (session, actor.ref ()).async ().get ();

    // dispatch 핸들러: join 요청을 수락하고, actor에게 온 메시지를 모은다.
    std::mutex mutex;
    std::vector<std::string> payloads;
    spot.set_dispatch_handler ([&] (zlink::service::spot_t &s,
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
                payloads.push_back (part->part.to_string ());
            }
        }
    });

    auto join = [&] (const char *payload) {
        std::promise<zlink::request_result_t> done;
        auto future = done.get_future ();
        zlink::message_t msg = zlink::message_t::from (payload);
        actor.join (spot)
          .message (msg)
          .timeout (std::chrono::seconds (1))
          .submit ([&done] (const zlink::actor_join_result_t &result,
                            std::vector<zlink::message_t>) { done.set_value (result.result); });
        assert (future.get () == zlink::request_result_t::ok);
    };
    auto send = [&] (const char *payload) {
        zlink::message_t msg = zlink::message_t::from (payload);
        stream.send_bound_actor (session, "single-player").message (msg).submit ();
    };
    auto leave = [&] () { (void) actor.leave (spot).async ().get (); };

    join ("join-first");  // actor가 spot에 합류
    send ("before");      // joined 상태에서 도착
    leave ();             // 처리 위치 이탈 (세션 바인딩은 유지)
    send ("between");     // leave 사이에 도착 → 큐잉
    join ("join-second"); // rejoin → 큐된 메시지가 핸들러로 배달된다

    for (int i = 0; i < 200; ++i) {
        {
            std::lock_guard<std::mutex> lock (mutex);
            if (payloads.size () >= 2)
                break;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
    {
        std::lock_guard<std::mutex> lock (mutex);
        assert (payloads.size () == 2 && payloads[0] == "before" && payloads[1] == "between");
    }

    leave ();
    actor.close ();
    std::printf (
      "[actor/single-player] queued payload: \"before/between\" -> actor: \"before/between\"\n");
    return 0;
}
