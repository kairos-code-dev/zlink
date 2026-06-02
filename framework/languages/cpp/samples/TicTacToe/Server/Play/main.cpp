/* SPDX-License-Identifier: MPL-2.0 */

#include "play_server_host_factory.hpp"

int
main ()
{
  using namespace zlink::samples::tictactoe;

  const sample_topology_t topology;
  auto zlink = play_server_host_factory_t::build (topology);
  if (zlink.channels ().size () != 1 || zlink.spot_nodes ().size () != 1 ||
      !zlink.spot_nodes ()[0].actor_gateway_enabled) {
    return 1;
  }

  create_match_room_handler_t room_handler;
  const auto created_room = room_handler.handle ({});
  tictactoe_match_room_t room (created_room.match_id);
  const auto created = room.create ({ sample_names_t::x_actor_id });
  join_match_handler_t join_handler (room);
  notification_inbox_t inbox;
  const auto joined =
    join_handler.handle ({ created.match_id, sample_names_t::o_actor_id });
  inbox.opponent_joined ({ created.match_id,
                           joined.actor_id,
                           joined.mark,
                           joined.state });
  inbox.turn_changed (
    { created.match_id, room.snapshot ().turn_actor_id, room.snapshot () });
  const auto state = play_finished_game (room, inbox, created.match_id);
  return state.status == "ended" &&
             state.winner_actor_id == sample_names_t::x_actor_id &&
             inbox.ended.size () == 1
           ? 0
           : 2;
}
