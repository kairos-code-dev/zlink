/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../tictactoe_entry_spot.hpp"

#include <string>
#include <utility>

namespace zlink::samples::tictactoe
{

class tictactoe_entry_spot_actor_joined_handler_t
{
public:
  void handle (entry_spot_t &spot, std::string actor_id) const
  {
    spot.actor_ids.push_back (std::move (actor_id));
  }
};

} // namespace zlink::samples::tictactoe
