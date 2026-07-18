/* SPDX-License-Identifier: MPL-2.0 */
/*
 * 자립형 가이드 예제: STREAM이 relay한 메시지를 Actor가 순서대로 처리.
 * Actor는 생성 시 Entry Spot(로비)에 있다가 join으로 개별 room(user Spot)으로
 * 옮겨 간다. 메시지는 STREAM session에 actor를 bind해 relay하며, actor의
 * application claim에서 들어온 순서대로 처리된다.
 */

#include "actor_sample_common.hpp"

int main ()
{
    // --8<-- [start:doc]
    zlink::context_t ctx;
    zlink::service::mesh_node_t node (ctx, {"sequential-mesh", ""});
    const zlink::routing_id_t node_rid = detail::mesh_start_single_node (node, "rooms");

    zlink::service::spot_t room = node.create_spot ();
    const zlink::routing_id_t room_rid = room.routing_id ();
    const uint64_t room_gen = room.status ().lifecycle_generation ();
    // 생성 직후 actor는 Entry Spot(로비)에 위치한다.
    zlink::service::actor_t player = node.create_actor ("player");

    // join으로 Entry Spot에서 room(user Spot)으로 이동한다.
    std::vector<zlink::message_t> hello = detail::make_parts ("enter-room");
    zlink::service::operation_id_t join_op;
    assert (player.join_spot (node_rid, room_rid, room_gen, hello, join_op,
                              std::chrono::seconds (1))
            == zlink::submit_result_t::ok);
    assert (actor_admit_join (node));

    // STREAM session에 actor를 bind하고, 플레이어 입력을 연달아 relay한다.
    actor_stream_session_t session (ctx, node);
    assert (session.bind (player.ref ()));
    const char *commands[] = {"move", "attack", "loot"};
    for (const char *command : commands)
        assert (session.relay (player.ref (), command));

    // actor는 들어온 순서대로 처리한다.
    std::vector<std::string> processed;
    for (int i = 0; i < 3; ++i) {
        std::string command;
        assert (actor_recv_text (node, command));
        processed.push_back (command);
    }
    assert (processed.size () == 3 && processed[0] == "move" && processed[1] == "attack"
            && processed[2] == "loot");

    std::printf ("[actor/sequential] processed in order: move -> attack -> loot\n");
    return 0;
    // --8<-- [end:doc]
}
