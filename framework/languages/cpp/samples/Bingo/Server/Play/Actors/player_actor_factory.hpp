/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "player_actor.hpp"

#include <utility>

namespace zlink::samples::bingo
{

struct player_actor_factory_t
{
  player_actor_t create (actor_ref_snapshot_t actor,
                         std::string display_name = {}) const
  {
    if (display_name.empty ()) {
      display_name = actor.actor_id;
    }
    return player_actor_t { std::move (actor), std::move (display_name) };
  }
};

} // namespace zlink::samples::bingo
