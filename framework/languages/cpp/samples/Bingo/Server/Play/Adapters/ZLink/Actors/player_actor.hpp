/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../../../Shared/Contracts/messages.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::bingo
{

struct player_actor_t
{
    mutable actor_ref_snapshot_t actor;
    mutable zlink::framework::actor_context_t context;
    std::string display_name;
    mutable bool destroy_after_entry_spot_join = false;
    mutable bool disconnected = false;

    void set_actor_ref (const zlink::framework::actor_ref_t &actor_ref) const
    {
        actor.actor_id = std::string (actor_ref.actor_id ());
        actor.generation = actor_ref.generation ();
    }

    void set_actor_context (const zlink::framework::actor_context_t &actor_context) const
    {
        context = actor_context;
    }

    void mark_for_destroy_after_room_leave () const { destroy_after_entry_spot_join = true; }

    void mark_disconnected () const { disconnected = true; }
};

} // namespace zlink::samples::bingo
