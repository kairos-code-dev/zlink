/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Shared/Contracts/messages.hpp"

#include <zlink/framework.hpp>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace zlink::samples::gamequest
{

class game_quest_client_scenario_t
{
  public:
    bool run (zlink::framework::channel_client_t &channels)
    {
        const auto subscribed =
          request<subscribe_quest_req_t, subscribe_quest_res_t> (channels, {"player-alice"});
        if (!subscribed.active_quests.empty ()) {
            return false;
        }

        const auto first_kill = request<kill_monster_req_t, event_res_t> (
          channels, {"player-alice", "wolf", "forest", "kill-1"});
        if (first_kill.event_id != "player-alice-kill-1") {
            return false;
        }
        request<kill_monster_req_t, event_res_t> (channels,
                                                  {"player-alice", "wolf", "forest", "kill-2"});
        const auto third_kill = request<kill_monster_req_t, event_res_t> (
          channels, {"player-alice", "wolf", "forest", "kill-3"});
        const auto duplicate = request<kill_monster_req_t, event_res_t> (
          channels, {"player-alice", "wolf", "forest", "kill-3"});
        if (duplicate.event_id != third_kill.event_id) {
            return false;
        }

        const auto auction = request<unlock_feature_req_t, event_res_t> (
          channels, {"player-alice", "auction", "unlock-auction"});
        if (auction.event_id != "player-alice-unlock-auction") {
            return false;
        }
        const auto snapshot = request<get_gameplay_snapshot_req_t, get_gameplay_snapshot_res_t> (
          channels, {"player-alice"});
        if (!contains (snapshot.unlocked_feature_ids, "auction")) {
            return false;
        }

        request<complete_mission_req_t, event_res_t> (
          channels, {"player-alice", "tutorial", "mission-tutorial"});
        request<enter_area_req_t, event_res_t> (
          channels, {"player-alice", "ruins", "enter-ruins"});

        request<collect_item_req_t, event_res_t> (
          channels, {"player-bob", "healing-herb", 1, "herb-1"});
        const auto bob_subscribed =
          request<subscribe_quest_req_t, subscribe_quest_res_t> (channels, {"player-bob"});
        if (!any_progress (bob_subscribed.active_quests, "herb-gathering", 1, "")) {
            return false;
        }
        request<collect_item_req_t, event_res_t> (
          channels, {"player-bob", "healing-herb", 4, "herb-2"});
        const auto bob_progress =
          request<get_quest_progress_req_t, get_quest_progress_res_t> (channels, {"player-bob"});
        if (!any_progress (bob_progress.active_quests, "herb-gathering", 5, "RewardGranted")) {
            return false;
        }

        request<delete_quest_projection_req_t, quest_progress_t> (
          channels, {"player-bob", "herb-gathering"});
        const auto missing =
          request<get_quest_progress_req_t, get_quest_progress_res_t> (channels, {"player-bob"});
        if (any_quest (missing.active_quests, "herb-gathering")) {
            return false;
        }
        const auto rebuilt = request<rebuild_quest_projection_req_t, quest_progress_t> (
          channels, {"player-bob", "herb-gathering"});
        if (rebuilt.quest_id != "herb-gathering" || rebuilt.status != "RewardGranted") {
            return false;
        }

        const auto sync =
          request<sync_quest_progress_req_t, sync_quest_progress_res_t> (channels,
                                                                        {"player-alice"});
        if (!any_progress (sync.updated_quests, "first-hunt", 4, "")) {
            return false;
        }
        const auto assertion =
          request<game_quest_server_assert_req_t, game_quest_server_assert_res_t> (channels, {});
        if (!assertion.passed) {
            return false;
        }
        std::cout << "gamequest-server-evidence=completed\n";
        return true;
    }

  private:
    template <typename TRequest, typename TResponse>
    static TResponse request (zlink::framework::channel_client_t &channels, TRequest request)
    {
        auto result = channels.request_to_channel ("gamequest.quest", std::move (request))
                        .template async<TResponse> ()
                        .result ();
        if (!result) {
            const auto *error = result.error ();
            throw std::runtime_error (error == nullptr ? "GameQuest request failed"
                                                       : error->what ());
        }
        return result.value ();
    }

    static bool contains (const std::vector<std::string> &values, const std::string &needle)
    {
        return std::find (values.begin (), values.end (), needle) != values.end ();
    }

    static bool any_quest (const std::vector<quest_progress_t> &values,
                           const std::string &quest_id)
    {
        return std::any_of (values.begin (), values.end (), [&] (const auto &progress) {
            return progress.quest_id == quest_id;
        });
    }

    static bool any_progress (const std::vector<quest_progress_t> &values,
                              const std::string &quest_id,
                              int min_count,
                              const std::string &status)
    {
        return std::any_of (values.begin (), values.end (), [&] (const auto &progress) {
            return progress.quest_id == quest_id && progress.current_count >= min_count &&
                   (status.empty () || progress.status == status);
        });
    }
};

} // namespace zlink::samples::gamequest
