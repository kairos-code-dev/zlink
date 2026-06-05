/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Actors/player_actor.hpp"

#include <zlink/framework.hpp>

#include <string>
#include <vector>

namespace zlink::samples::bingo
{

struct bingo_entry_spot_t
{
    void configure (zlink::framework::spot_context_t &context);

    std::vector<std::string> joined_actor_ids;
};

} // namespace zlink::samples::bingo

#include "Handlers/bingo_entry_spot_actor_joined_handler.hpp"
#include "Handlers/bingo_entry_spot_actor_left_handler.hpp"
#include "Handlers/match_bingo_actor_handler.hpp"

namespace zlink::samples::bingo
{

inline void bingo_entry_spot_t::configure (zlink::framework::spot_context_t &context)
{
    context.handlers ()
      .add_actor_packet<match_bingo_actor_handler_t> ()
      .add_post_actor_joined<bingo_entry_spot_actor_joined_handler_t> ()
      .add_actor_left<bingo_entry_spot_actor_left_handler_t> ();
}

} // namespace zlink::samples::bingo
