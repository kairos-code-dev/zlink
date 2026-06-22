/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../../../Shared/Contracts/messages.hpp"

#include <zlink/framework.hpp>

#include <string>

namespace zlink::samples::supportchat
{

struct support_user_actor_t
{
    mutable actor_ref_snapshot_t actor;
    mutable zlink::framework::actor_context_t context;
    std::string display_name;
    std::string role;
    mutable std::string conversation_id;
    mutable bool destroy_after_entry_spot_join = false;
    mutable bool disconnected = false;

    const std::string &actor_id () const noexcept { return actor.actor_id; }

    void set_actor_ref (const zlink::framework::actor_ref_t &actor_ref) const
    {
        actor.actor_id = std::string (actor_ref.actor_id ());
        actor.generation = actor_ref.generation ();
    }

    void set_actor_context (const zlink::framework::actor_context_t &actor_context) const
    {
        context = actor_context;
    }

    void set_identity (std::string new_display_name, std::string new_role)
    {
        display_name = std::move (new_display_name);
        role = std::move (new_role);
    }

    void join_conversation (std::string id) const { conversation_id = std::move (id); }

    void mark_for_destroy_after_room_leave () const { destroy_after_entry_spot_join = true; }

    void mark_disconnected () const { disconnected = true; }
};

} // namespace zlink::samples::supportchat
