/* SPDX-License-Identifier: MPL-2.0 */
/*
 * 자립형 가이드 예제: STREAM 게이트웨이 → play-session Actor relay.
 * 게이트웨이 노드가 play-session actor를 호스트하고, 클라이언트 입력을 STREAM
 * session으로 받아 actor에게 relay한다.
 *
 * RouteMesh 10.0.0: gateway 노드가 actor를 만들고 play Spot에 조인시킨 뒤, STREAM
 * session에 bind한다. 클라이언트가 보낸 프레임이 actor로 relay된다.
 */

#include "actor_sample_common.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::service::mesh_node_t node (ctx, {"gateway-mesh", ""});
    const zlink::routing_id_t node_rid = detail::mesh_start_single_node (node, "sessions");

    zlink::service::spot_t play_spot = node.create_spot ();
    const zlink::routing_id_t play_rid = play_spot.routing_id ();
    const uint64_t play_gen = play_spot.status ().lifecycle_generation ();
    zlink::service::actor_t play_actor = node.create_actor ("play-session-actor");
    assert (play_actor.valid ());

    // play-session actor를 play Spot에 조인시키고 수락한다.
    std::vector<zlink::message_t> hello = detail::make_parts ("join-play");
    zlink::service::operation_id_t join_op;
    assert (play_actor.join_spot (node_rid, play_rid, play_gen, hello, join_op,
                                  std::chrono::seconds (1))
            == zlink::submit_result_t::ok);
    assert (actor_admit_join (node));

    // 게이트웨이 STREAM session이 클라이언트 입력을 actor로 relay한다.
    actor_stream_session_t session (ctx, node);
    assert (session.bind (play_actor.ref ()));
    assert (session.relay (play_actor.ref (), "client-input"));

    std::string payload;
    assert (actor_recv_text (node, payload));

    // actor 정리.
    zlink::service::operation_id_t leave_op;
    (void) play_actor.leave_spot (0, leave_op, std::chrono::seconds (1));
    zlink::service::operation_id_t destroy_op;
    (void) play_actor.destroy (destroy_op, std::chrono::seconds (1));

    std::printf ("[actor/gateway] stream payload: \"client-input\" -> actor: \"%s\"\n",
                 payload.c_str ());
    return 0;
}
