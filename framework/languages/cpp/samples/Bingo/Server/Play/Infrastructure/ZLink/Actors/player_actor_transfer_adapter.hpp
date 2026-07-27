/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "player_actor.hpp"

namespace zlink::samples::bingo
{

struct player_actor_state_t
{
    actor_ref_snapshot_t actor;
    std::string display_name;
    bool destroy_after_entry_spot_join = false;
    bool disconnected = false;
};

inline void to_json (nlohmann::json &json, const player_actor_state_t &value)
{
    json = {{"actor", value.actor},
            {"displayName", value.display_name},
            {"destroyAfterEntrySpotJoin", value.destroy_after_entry_spot_join},
            {"disconnected", value.disconnected}};
}

inline void from_json (const nlohmann::json &json, player_actor_state_t &value)
{
    value.actor = json.value ("actor", actor_ref_snapshot_t{});
    value.display_name = json.value ("displayName", std::string{});
    value.destroy_after_entry_spot_join =
      json.value ("destroyAfterEntrySpotJoin", false);
    value.disconnected = json.value ("disconnected", false);
}

class player_actor_transfer_adapter_t final
    : public actor_transfer_adapter_t<player_actor_t>
{
  public:
    task_t<message_t> transfer_out (const player_actor_t &actor) override
    {
        return task_t<message_t> (
          result_t<message_t>::success (message_t::from (player_actor_state_t{
            actor.actor, actor.display_name, actor.destroy_after_entry_spot_join,
            actor.disconnected})));
    }

    task_t<player_actor_t> transfer_in (std::string actor_id, message_t state) override
    {
        auto restored = state.decode<player_actor_state_t> ();
        restored.actor.actor_id = std::move (actor_id);
        player_actor_t actor;
        actor.actor = std::move (restored.actor);
        actor.display_name = std::move (restored.display_name);
        actor.destroy_after_entry_spot_join = restored.destroy_after_entry_spot_join;
        actor.disconnected = restored.disconnected;
        return task_t<player_actor_t> (result_t<player_actor_t>::success (std::move (actor)));
    }
};

} // namespace zlink::samples::bingo
