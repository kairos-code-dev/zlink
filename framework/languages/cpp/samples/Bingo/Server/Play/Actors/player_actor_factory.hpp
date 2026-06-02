/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "player_actor.hpp"

#include <utility>

namespace zlink::samples::bingo
{

struct player_actor_factory_t
{
  player_actor_t create (actor_ref_snapshot_t actor) const
  {
    return player_actor_t { std::move (actor) };
  }
};

} // namespace zlink::samples::bingo
