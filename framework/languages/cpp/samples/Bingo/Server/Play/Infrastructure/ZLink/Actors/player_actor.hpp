/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../../../Shared/Contracts/messages.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::bingo
{

using namespace framework;
using framework::actor_ref_t;
using framework::message_t;

struct player_actor_t
{
    mutable actor_ref_snapshot_t actor;
    mutable actor_context_t context;
    std::string display_name;
    mutable bool destroy_after_entry_spot_join = false;
    mutable bool disconnected = false;

    void set_actor_ref (const actor_ref_t &actor_ref) const
    {
        actor.node_rid = std::string (actor_ref.node_rid ().value ());
        actor.actor_id = std::string (actor_ref.actor_id ());
        actor.generation = actor_ref.generation ();
    }

    void set_actor_context (const actor_context_t &actor_context) const { context = actor_context; }

    void mark_for_destroy_after_room_leave () const { destroy_after_entry_spot_join = true; }

    void mark_disconnected () const { disconnected = true; }

    template <typename TNotify> auto push (const TNotify &notify) const
    {
        return context.bound_session ().send (notify).async ();
    }
};

} // namespace zlink::samples::bingo
