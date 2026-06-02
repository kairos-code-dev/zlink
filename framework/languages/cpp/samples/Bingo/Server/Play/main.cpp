/* SPDX-License-Identifier: MPL-2.0 */

#include "play_server_host_factory.hpp"

int
main ()
{
  using namespace zlink::samples::bingo;

  const sample_topology_t topology;
  auto zlink = play_server_host_factory_t::build (topology);
  if (zlink.channels ().size () != 2 || zlink.spot_nodes ().size () != 1) {
    return 1;
  }

  bingo_room_directory_t rooms;
  allocate_bingo_room_handler_t allocator (rooms);
  ensure_player_actor_handler_t actor_handler;
  const auto actor =
    actor_handler.handle ({ "player-1", "Player 1" });
  const auto match = allocator.handle ({ "four-player" });
  auto &room = rooms.get (match.room_id);
  bingo_room_join_handler_t join_handler (rooms);
  start_bingo_game_handler_t start_handler (rooms);
  leave_room_handler_t leave_handler (rooms);
  bingo_notification_inbox_t inbox;
  const auto join1 = join_handler.handle (
    { match.room_id, actor.actor_id, "Player 1" });
  const auto join2 = join_handler.handle (
    { match.room_id, "player-2", "Player 2" });
  inbox.on_joined ({ match.room_id, "player-1", "Player 1", 1, true,
                     join1.state });
  inbox.on_joined ({ match.room_id, "player-2", "Player 2", 2, false,
                     join2.state });
  const auto started = start_handler.handle ({ match.room_id });
  inbox.on_started ({ started.state });
  if (!started.state.can_start || started.state.status != "playing") {
    return 2;
  }

  auto publisher = zlink.publisher ();
  bool callback_submit_seen = false;
  const auto drawn = room.draw (7);
  publisher
    .publish (sample_names_t::notification_channel,
              number_drawn_notify_t::packet_name,
              drawn)
    .submit ([&](zlink::framework::result_t<void> result) {
      callback_submit_seen = static_cast<bool> (result);
    });
  inbox.on_drawn (drawn);
  if (!callback_submit_seen ||
      !publish_drawn_async (publisher, room.draw (11)).result () ||
      inbox.drawn.front ().number != 7) {
    return 3;
  }
  inbox.on_state ({ room.snapshot () });
  inbox.on_ended ({ room.draw (1).state });
  const auto left = leave_handler.handle ({ match.room_id }, "player-2");
  if (left.state.players.size () != 1 || inbox.started.empty () ||
      inbox.states.empty () || inbox.ended.empty ()) {
    return 4;
  }

  zlink::framework::spot_node_builder_t spot_node;
  spot_node.add_spot<bingo_room_t> (sample_names_t::room_spot);
  auto spot = spot_node.create_spot (sample_names_t::room_spot);
  auto timer = spot.add_timer<bingo_room_t> (
    "draw-number",
    std::chrono::milliseconds (20),
    { .overrun_policy =
        zlink::framework::timer_overrun_policy_t::skip_late_ticks });
  return timer.is_disposed () ? 5 : 0;
}
