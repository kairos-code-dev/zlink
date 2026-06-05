/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../tictactoe_entry_spot.hpp"

#include <zlink/framework.hpp>

#include <algorithm>
#include <string>

namespace zlink::samples::tictactoe
{

class tictactoe_entry_spot_actor_left_handler_t
{
  public:
    using spot_type = entry_spot_t;
    using actor_type = player_actor_t;

    void
    handle (entry_spot_t &spot, const player_actor_t &actor, const zlink::framework::spot_actor_change_result_t &) const
    {
        spot.actor_ids.erase (std::remove (spot.actor_ids.begin (), spot.actor_ids.end (), actor.actor_id),
                              spot.actor_ids.end ());
    }
};

} // namespace zlink::samples::tictactoe
