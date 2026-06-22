/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "player_actor.hpp"

#include <utility>

namespace zlink::samples::bingo
{

struct player_actor_factory_t
{
    player_actor_t create (std::string actor_id) const
    {
        return create (actor_ref_snapshot_t{"", std::move (actor_id), 0});
    }

    player_actor_t create (actor_ref_snapshot_t actor, std::string display_name = {}) const
    {
        if (display_name.empty ()) {
            display_name = actor.actor_id;
        }
        player_actor_t player;
        player.actor = std::move (actor);
        player.display_name = std::move (display_name);
        return player;
    }
};

} // namespace zlink::samples::bingo
