/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "player_actor.hpp"

namespace zlink::samples::tictactoe
{

struct player_actor_state_t
{
    std::string actor_id;
    std::string node_rid;
    unsigned long long generation = 1;
    bool destroy_after_entry_spot_join = false;
    bool disconnected = false;
    player_info_t player;
};

inline void to_json (nlohmann::json &json, const player_actor_state_t &value)
{
    json = nlohmann::json{{"actorId", value.actor_id},
                          {"nodeRid", value.node_rid},
                          {"generation", value.generation},
                          {"destroyAfterEntrySpotJoin", value.destroy_after_entry_spot_join},
                          {"disconnected", value.disconnected},
                          {"player", value.player}};
}

inline void from_json (const nlohmann::json &json, player_actor_state_t &value)
{
    value.actor_id = json.value ("actorId", std::string{});
    value.node_rid = json.value ("nodeRid", std::string{});
    value.generation = json.value ("generation", 1ull);
    value.destroy_after_entry_spot_join =
      json.value ("destroyAfterEntrySpotJoin", false);
    value.disconnected = json.value ("disconnected", false);
    value.player = json.value ("player", player_info_t{});
}

class player_actor_transfer_adapter_t final
    : public actor_transfer_adapter_t<player_actor_t>
{
  public:
    task_t<message_t> transfer_out (const player_actor_t &actor) override
    {
        return task_t<message_t> (
          result_t<message_t>::success (message_t::from (player_actor_state_t{
            actor.actor_id, actor.node_rid, actor.generation,
            actor.destroy_after_entry_spot_join, actor.disconnected, actor.player})));
    }

    task_t<player_actor_t> transfer_in (std::string actor_id, message_t state) override
    {
        auto restored = state.decode<player_actor_state_t> ();
        player_actor_t actor;
        actor.actor_id = std::move (actor_id);
        actor.node_rid = std::move (restored.node_rid);
        actor.generation = restored.generation;
        actor.destroy_after_entry_spot_join = restored.destroy_after_entry_spot_join;
        actor.disconnected = restored.disconnected;
        actor.player = std::move (restored.player);
        return task_t<player_actor_t> (result_t<player_actor_t>::success (std::move (actor)));
    }
};

} // namespace zlink::samples::tictactoe
