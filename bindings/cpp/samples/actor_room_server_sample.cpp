/* SPDX-License-Identifier: MPL-2.0 */
/*
 * 자립형 가이드 예제: STREAM 게이트웨이가 relay한 패킷을 room의 Actor가 받는다.
 *
 * RouteMesh 10.0.0: actor를 STREAM session에 bind하면, 그 session으로 들어온
 * 패킷이 actor로 relay된다. actor는 pull 루프의 application claim에서 받는다.
 */

#include "actor_sample_common.hpp"

int main ()
{
    zlink::context_t ctx;
    zlink::service::mesh_node_t node (ctx, {"session-mesh", ""});
    const zlink::routing_id_t node_rid = detail::mesh_start_single_node (node, "sessions");

    zlink::service::spot_t room = node.create_spot ();
    const zlink::routing_id_t room_rid = room.routing_id ();
    const uint64_t room_gen = room.status ().lifecycle_generation ();
    zlink::service::actor_t actor = node.create_actor ("room-player-1");

    // actor가 room에 조인하고, room이 수락한다.
    std::vector<zlink::message_t> hello = detail::make_parts ("enter-room");
    zlink::service::operation_id_t join_op;
    assert (actor.join_spot (node_rid, room_rid, room_gen, hello, join_op,
                             std::chrono::seconds (1))
            == zlink::submit_result_t::ok);
    assert (actor_admit_join (node));

    // STREAM session에 actor를 bind하고, 클라이언트 패킷을 relay한다.
    actor_stream_session_t session (ctx, node);
    assert (session.bind (actor.ref ()));
    assert (session.relay (actor.ref (), "move:north"));

    std::string payload;
    assert (actor_recv_text (node, payload));
    std::printf ("[actor/room] stream payload: \"move:north\" -> actor: \"%s\"\n",
                 payload.c_str ());
    return 0;
}
