/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <string>
#include <utility>

#include <zlink/framework.hpp>

namespace zlink::samples::tictactoe
{

struct player_actor_t
{
    std::string actor_id;
    mutable unsigned long long generation = 1;
    mutable bool destroy_after_entry_spot_join = false;
    mutable bool disconnected = false;
    mutable zlink::framework::actor_context_t context;

    void set_actor_ref (const zlink::framework::actor_ref_t &actor_ref) const
    {
        generation = actor_ref.generation ();
    }

    void set_actor_context (zlink::framework::actor_context_t actor_context) const
    {
        context = std::move (actor_context);
    }

    void mark_for_destroy_after_room_leave () const { destroy_after_entry_spot_join = true; }

    void mark_disconnected () const { disconnected = true; }
};

struct player_actor_factory_t
{
    player_actor_t create (std::string actor_id) const { return {std::move (actor_id)}; }
};

struct move_packet_t
{
    int cell = 0;
};

} // namespace zlink::samples::tictactoe
