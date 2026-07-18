/* SPDX-License-Identifier: MPL-2.0 */
/*
 * 자립형 가이드 예제: Actor 재접속 큐잉(같은 Spot 재조인).
 * actor가 Spot을 떠난 사이 도착한 메시지는 session binding에 큐잉되고,
 * 같은 Spot으로 재조인하면 그 메시지가 배달된다.
 */

#include "actor_sample_common.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::service::mesh_node_t node (ctx, {"queue-mesh", ""});
    const zlink::routing_id_t node_rid = detail::mesh_start_single_node (node, "rooms");

    zlink::service::spot_t spot = node.create_spot ();
    const zlink::routing_id_t spot_rid = spot.routing_id ();
    zlink::service::actor_t actor = node.create_actor ("single-player");

    actor_stream_session_t session (ctx, node);
    assert (session.bind (actor.ref ()));

    auto join = [&] (const char *payload) {
        std::vector<zlink::message_t> hello = detail::make_parts (payload);
        zlink::service::operation_id_t op_id;
        assert (actor.join_spot (node_rid, spot_rid, spot.status ().lifecycle_generation (), hello,
                                 op_id, std::chrono::seconds (1))
                == zlink::submit_result_t::ok);
        assert (actor_admit_join (node));
    };

    join ("join-first");                        // actor가 spot에 합류
    assert (session.relay (actor.ref (), "before")); // joined 상태에서 도착
    std::string before;
    assert (actor_recv_text (node, before));

    zlink::service::operation_id_t leave_op;
    (void) actor.leave_spot (0, leave_op, std::chrono::seconds (1)); // 이탈 (session 유지)
    assert (session.relay (actor.ref (), "between"));                // 이탈 중 도착 → 큐잉

    join ("join-second");                       // 재조인 → 큐된 메시지 배달
    std::string between;
    assert (actor_recv_text (node, between));

    assert (before == "before" && between == "between");
    std::printf ("[actor/queue] queued payload: \"before/between\" -> actor: \"%s/%s\"\n",
                 before.c_str (), between.c_str ());
    return 0;
}
