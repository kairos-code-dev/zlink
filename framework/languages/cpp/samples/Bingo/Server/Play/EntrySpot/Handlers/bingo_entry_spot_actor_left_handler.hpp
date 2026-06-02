/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../bingo_entry_spot.hpp"

#include <zlink/framework.hpp>

#include <algorithm>
#include <string>

namespace zlink::samples::bingo
{

class bingo_entry_spot_actor_left_handler_t
{
public:
  using spot_type = bingo_entry_spot_t;
  using actor_type = player_actor_t;

  void handle (bingo_entry_spot_t &spot,
               const player_actor_t &actor,
               const zlink::framework::spot_actor_change_result_t &) const
  {
    spot.joined_actor_ids.erase (
      std::remove (spot.joined_actor_ids.begin (),
                   spot.joined_actor_ids.end (),
                   actor.actor.actor_id),
      spot.joined_actor_ids.end ());
  }
};

} // namespace zlink::samples::bingo
