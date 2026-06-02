/* SPDX-License-Identifier: MPL-2.0 */

#include "api_server_host_factory.hpp"

int
main ()
{
  using namespace zlink::samples::tictactoe;

  const sample_topology_t topology;
  auto zlink = api_server_host_factory_t::build (topology);
  if (zlink.channels ().size () != 1) {
    return 1;
  }

  authenticate_actor_handler_t auth;
  ensure_player_actor_handler_t actor_handler;
  create_match_room_handler_t room_handler;
  create_match_handler_t create_handler (room_handler);
  const auto accepted = auth.handle ({ "player-1" });
  const auto rejected = auth.handle ({ "" });
  const auto actor = actor_handler.handle ({ accepted.actor_id });
  const auto match = create_handler.handle ({ accepted.actor_id });
  return accepted.accepted && !rejected.accepted &&
             actor.actor_type == sample_names_t::actor_type &&
             !match.match_id.empty ()
           ? 0
           : 2;
}
