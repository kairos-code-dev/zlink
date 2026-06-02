/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../tictactoe_entry_spot.hpp"

#include <algorithm>
#include <string>

namespace zlink::samples::tictactoe
{

class tictactoe_entry_spot_actor_left_handler_t
{
public:
  void handle (entry_spot_t &spot, const std::string &actor_id) const
  {
    spot.actor_ids.erase (
      std::remove (spot.actor_ids.begin (), spot.actor_ids.end (), actor_id),
      spot.actor_ids.end ());
  }
};

} // namespace zlink::samples::tictactoe
