/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/Actors/player_actor.hpp"

#include <zlink/framework.hpp>

#include <string>
#include <vector>

namespace zlink::samples::tictactoe
{

struct entry_spot_t
{
  void configure (zlink::framework::spot_context_t &context);

  std::vector<std::string> actor_ids;
};

} // namespace zlink::samples::tictactoe

#include "Handlers/join_match_handler.hpp"
#include "Handlers/tictactoe_entry_spot_actor_joined_handler.hpp"
#include "Handlers/tictactoe_entry_spot_actor_left_handler.hpp"

namespace zlink::samples::tictactoe
{

inline void
entry_spot_t::configure (zlink::framework::spot_context_t &context)
{
  context.handlers ()
    .add_actor_packet<join_match_handler_t> ()
    .add_post_actor_joined<tictactoe_entry_spot_actor_joined_handler_t> ()
    .add_actor_left<tictactoe_entry_spot_actor_left_handler_t> ();
}

} // namespace zlink::samples::tictactoe
