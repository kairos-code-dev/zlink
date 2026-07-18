/* SPDX-License-Identifier: MPL-2.0 */
/*
 * 자립형 가이드 예제: SPOT Actor의 재접속 이전성(single-player queue).
 * actor가 한 Spot을 떠나 다른 Spot으로 옮기는 사이에 도착한 메시지는
 * session binding에 큐잉되어, 재조인 후 순서대로 배달된다.
 */

#include "actor_sample_common.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::service::mesh_node_t node (ctx, {"single-player-mesh", ""});
    const zlink::routing_id_t node_rid = detail::mesh_start_single_node (node, "rooms");

    zlink::service::spot_t first_spot = node.create_spot ();
    zlink::service::spot_t second_spot = node.create_spot ();
    zlink::service::actor_t actor = node.create_actor ("single-player");

    // STREAM session에 actor를 bind한다 (조인/이탈과 무관하게 유지된다).
    actor_stream_session_t session (ctx, node);
    assert (session.bind (actor.ref ()));

    // 첫 Spot에 조인 → "before"가 조인 상태에서 도착한다.
    std::vector<zlink::message_t> hello_first = detail::make_parts ("join-first");
    zlink::service::operation_id_t op_first;
    assert (actor.join_spot (node_rid, first_spot.routing_id (),
                             first_spot.status ().lifecycle_generation (), hello_first, op_first,
                             std::chrono::seconds (1))
            == zlink::submit_result_t::ok);
    assert (actor_admit_join (node));
    assert (session.relay (actor.ref (), "before"));
    std::string first_payload;
    assert (actor_recv_text (node, first_payload));

    // 첫 Spot을 떠난다 (session 바인딩은 유지). 그 사이 "between"이 도착해 큐잉.
    zlink::service::operation_id_t leave_op;
    (void) actor.leave_spot (0, leave_op, std::chrono::seconds (1));
    assert (session.relay (actor.ref (), "between"));

    // 둘째 Spot으로 재조인 → 큐된 "between"이 배달된다.
    std::vector<zlink::message_t> hello_second = detail::make_parts ("join-second");
    zlink::service::operation_id_t op_second;
    assert (actor.join_spot (node_rid, second_spot.routing_id (),
                             second_spot.status ().lifecycle_generation (), hello_second, op_second,
                             std::chrono::seconds (1))
            == zlink::submit_result_t::ok);
    assert (actor_admit_join (node));
    std::string second_payload;
    assert (actor_recv_text (node, second_payload));

    std::printf ("[actor/single-player] queued payload: \"before/between\" -> actor: \"%s/%s\"\n",
                 first_payload.c_str (), second_payload.c_str ());
    return 0;
}
