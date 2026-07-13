/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <string>
#include <utility>

#include <zlink/framework.hpp>

#include "../../../../Configuration/sample_names.hpp"
#include "../../../../../Shared/Contracts/messages.hpp"

namespace zlink::samples::tictactoe
{

using namespace framework;
using framework::actor_ref_t;
using framework::message_t;

struct player_actor_t
{
    std::string actor_id;
    mutable std::string node_rid;
    mutable unsigned long long generation = 1;
    mutable bool destroy_after_entry_spot_join = false;
    mutable bool disconnected = false;
    mutable player_info_t player;
    mutable actor_context_t context;

    void set_actor_ref (const actor_ref_t &actor_ref) const
    {
        node_rid = std::string (actor_ref.node_rid ().value ());
        generation = actor_ref.generation ();
    }

    void set_actor_context (actor_context_t actor_context) const
    {
        context = std::move (actor_context);
    }

    void mark_for_destroy_after_room_leave () const { destroy_after_entry_spot_join = true; }

    void mark_disconnected () const { disconnected = true; }

    void apply_player (player_info_t value) const { player = std::move (value); }

    template <typename TNotify> void push (const TNotify &notify) const
    {
        context.bound_session ().send (notify).submit ();
    }

    player_info_t require_player () const
    {
        if (player.actor_id.empty ()) {
            return {actor_id, actor_id, sample_names_t::required_level, 0};
        }
        return player;
    }

    int increment_wins () const { return ++player.wins; }
};

inline void to_json (nlohmann::json &json, const player_actor_t &value)
{
    json = nlohmann::json{{"actorId", value.actor_id},
                          {"nodeRid", value.node_rid},
                          {"generation", value.generation},
                          {"destroyAfterEntrySpotJoin", value.destroy_after_entry_spot_join},
                          {"disconnected", value.disconnected},
                          {"player", value.player}};
}

inline void from_json (const nlohmann::json &json, player_actor_t &value)
{
    value.actor_id = json.value ("actorId", std::string{});
    value.node_rid = json.value ("nodeRid", std::string{});
    value.generation = json.value ("generation", 1ull);
    value.destroy_after_entry_spot_join = json.value ("destroyAfterEntrySpotJoin", false);
    value.disconnected = json.value ("disconnected", false);
    value.player = json.value ("player", player_info_t{});
}

struct player_actor_factory_t
{
    player_actor_t create (std::string actor_id) const { return {std::move (actor_id)}; }
};

struct move_packet_t
{
    int cell = 0;
};

} // namespace zlink::samples::tictactoe
