/* SPDX-License-Identifier: MPL-2.0 */
/*
 * 자립형 가이드 예제: 한 방(Spot)의 두 플레이어(Actor).
 * 서버가 각 플레이어에게 actor ref로 주소 지정해 메시지를 보내면, 그 Actor만 받는다.
 *
 * RouteMesh 10.0.0: actor는 mesh 노드의 Spot에 조인하고, 노드는 actor ref로
 * 직접 메시지를 보낸다. 각 메시지는 pull 루프에서 그 actor의 application claim으로
 * 도착한다.
 */

#include "actor_sample_common.hpp"

int main ()
{
    // --8<-- [start:doc]
    zlink::context_t ctx;
    zlink::service::mesh_node_t node (ctx, {"room-mesh", ""});
    const zlink::routing_id_t node_rid = detail::mesh_start_single_node (node, "rooms");

    zlink::service::spot_t room = node.create_spot ();
    const zlink::routing_id_t room_rid = room.routing_id ();
    const uint64_t room_gen = room.status ().lifecycle_generation ();

    zlink::service::actor_t player1 = node.create_actor ("player-1");
    zlink::service::actor_t player2 = node.create_actor ("player-2");

    // 각 플레이어가 방에 조인하고, 방(Spot)이 admission을 수락한다.
    auto join_room = [&] (zlink::service::actor_t &player) {
        std::vector<zlink::message_t> hello = detail::make_parts ("enter-room");
        zlink::service::operation_id_t op_id;
        assert (player.join_spot (node_rid, room_rid, room_gen, hello, op_id,
                                  std::chrono::seconds (1))
                == zlink::submit_result_t::ok);
        assert (actor_admit_join (node));
    };
    join_room (player1);
    join_room (player2);

    // 서버가 각 플레이어에게 자기 앞으로 온 메시지를 보낸다.
    std::vector<zlink::message_t> to_p1 = detail::make_parts ("your-turn");
    assert (node.send_to_actor (player1.ref (), to_p1) == zlink::submit_result_t::ok);
    std::string got_p1;
    assert (actor_recv_text (node, got_p1));

    std::vector<zlink::message_t> to_p2 = detail::make_parts ("wait");
    assert (node.send_to_actor (player2.ref (), to_p2) == zlink::submit_result_t::ok);
    std::string got_p2;
    assert (actor_recv_text (node, got_p2));

    std::printf ("[actor/room] player-1: \"%s\", player-2: \"%s\"\n", got_p1.c_str (),
                 got_p2.c_str ());
    return 0;
    // --8<-- [end:doc]
}
