/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../bingo_entry_spot.hpp"

#include <algorithm>
#include <string>

namespace zlink::samples::bingo
{

class bingo_entry_spot_actor_left_handler_t
{
public:
  void handle (bingo_entry_spot_t &spot, const std::string &actor_id) const
  {
    spot.joined_actor_ids.erase (
      std::remove (spot.joined_actor_ids.begin (),
                   spot.joined_actor_ids.end (),
                   actor_id),
      spot.joined_actor_ids.end ());
  }
};

} // namespace zlink::samples::bingo
