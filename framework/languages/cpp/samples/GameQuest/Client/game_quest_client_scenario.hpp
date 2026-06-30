/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Shared/Contracts/messages.hpp"

#include <zlink/http_client.hpp>
#include <zlink/stream_connector.hpp>
#include <zlink/stream_e2e_client.hpp>
#include <zlink/stream_e2e_client/codecs/auto_codec.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

namespace zlink::samples::gamequest
{

class game_quest_client_scenario_t
{
  public:
    bool run (const std::string &api_url, const std::string &stream_endpoint)
    {
        try {
            auto http = zlink::http_client::client_t::create (api_url)
                          .timeout (std::chrono::seconds (12))
                          .build ();
            zlink::stream_connector::connector_options_t options;
            options.endpoint = stream_endpoint;
            options.connect_timeout = std::chrono::seconds (5);
            options.request_timeout = std::chrono::seconds (10);
            options.dispatch_mode = zlink::stream_connector::dispatch_mode_t::immediate;
            auto core = zlink::stream_connector::connector_factory_t::create (options);
            auto stream = zlink::stream_e2e_client::use (core);
            ensure (static_cast<bool> (stream.connect ().submit ()), "stream connect failed");

            const auto subscribed =
              stream.request (subscribe_quest_req_t{"player-alice"})
                .async<subscribe_quest_res_t> ()
                .result ();
            ensure (subscribed && subscribed.value ().active_quests.empty (),
                    "initial subscribe failed");

            auto first_progress =
              stream.wait_for<quest_progress_t> ("QuestProgressNotify")
                .where ([] (const quest_progress_t &progress) {
                    return progress.player_id == "player-alice"
                           && progress.quest_id == "first-hunt"
                           && progress.current_count == 1;
                })
                .timeout (std::chrono::seconds (10))
                .to_future ("first quest progress wait failed");
            auto first_kill = http.post ("/combat/kill")
                                .body (kill_monster_req_t{
                                  "player-alice", "wolf", "forest", "kill-1"})
                                .fetch<kill_monster_res_t> ();
            ensure (first_kill.event_id == "player-alice-kill-1", "first kill failed");
            (void) first_progress.get ();

            auto first_completed =
              stream.wait_for<quest_progress_t> ("QuestCompletedNotify")
                .where ([] (const quest_progress_t &progress) {
                    return progress.player_id == "player-alice"
                           && progress.quest_id == "first-hunt"
                           && progress.status == "RewardGranted";
                })
                .timeout (std::chrono::seconds (10))
                .to_future ("first quest completed wait failed");
            (void) http.post ("/combat/kill")
              .body (kill_monster_req_t{"player-alice", "wolf", "forest", "kill-2"})
              .fetch<kill_monster_res_t> ();
            const auto third_kill = http.post ("/combat/kill")
                                      .body (kill_monster_req_t{
                                        "player-alice", "wolf", "forest", "kill-3"})
                                      .fetch<kill_monster_res_t> ();
            ensure (third_kill.event_id == "player-alice-kill-3", "third kill failed");
            (void) first_completed.get ();

            const auto duplicate = http.post ("/combat/kill")
                                     .body (kill_monster_req_t{
                                       "player-alice", "wolf", "forest", "kill-3"})
                                     .fetch<kill_monster_res_t> ();
            ensure (duplicate.event_id == third_kill.event_id, "idempotency failed");

            auto auction_completed =
              stream.wait_for<quest_progress_t> ("QuestCompletedNotify")
                .where ([] (const quest_progress_t &progress) {
                    return progress.player_id == "player-alice"
                           && progress.quest_id == "open-auction";
                })
                .timeout (std::chrono::seconds (10))
                .to_future ("auction quest completed wait failed");
	            const auto auction = http.post ("/feature/unlock")
	                                   .body (unlock_feature_req_t{
	                                     "player-alice", "auction", "unlock-auction"})
	                                   .fetch<unlock_feature_res_t> ();
            ensure (auction.event_id == "player-alice-unlock-auction", "auction unlock failed");
            (void) auction_completed.get ();

            const auto snapshot = http.post ("/internal/snapshot")
                                    .body (get_gameplay_snapshot_req_t{"player-alice"})
                                    .fetch<get_gameplay_snapshot_res_t> ();
            ensure (contains (snapshot.unlocked_feature_ids, "auction"), "snapshot failed");

	            (void) http.post ("/mission/complete")
	              .body (complete_mission_req_t{"player-alice", "tutorial", "mission-tutorial"})
	              .fetch<complete_mission_res_t> ();
	            (void) http.post ("/world/enter")
	              .body (enter_area_req_t{"player-alice", "ruins", "enter-ruins"})
	              .fetch<enter_area_res_t> ();

	            (void) http.post ("/inventory/collect")
	              .body (collect_item_req_t{"player-bob", "healing-herb", 1, "herb-1"})
	              .fetch<collect_item_res_t> ();
            const auto bob =
              stream.request (subscribe_quest_req_t{"player-bob"})
                .async<subscribe_quest_res_t> ()
                .result ();
            ensure (bob && any_progress (bob.value ().active_quests, "herb-gathering", 1, ""),
                    "bob subscribe failed");
	            (void) http.post ("/inventory/collect")
	              .body (collect_item_req_t{"player-bob", "healing-herb", 4, "herb-2"})
	              .fetch<collect_item_res_t> ();
            const auto bob_progress =
              stream.request (get_quest_progress_req_t{"player-bob"})
                .async<get_quest_progress_res_t> ()
                .result ();
            ensure (bob_progress
                      && any_progress (bob_progress.value ().active_quests, "herb-gathering", 5,
                                       "RewardGranted"),
                    "bob progress failed");

            (void) http.post ("/self-check/projection/delete")
              .body (delete_quest_projection_req_t{"player-bob", "herb-gathering"})
              .fetch<quest_progress_t> ();
            auto missing = stream.request (get_quest_progress_req_t{"player-bob"})
                             .async<get_quest_progress_res_t> ()
                             .result ();
            ensure (missing
                      && !any_progress (missing.value ().active_quests, "herb-gathering", 1, ""),
                    "delete projection failed");
            const auto rebuilt = http.post ("/self-check/projection/rebuild")
                                   .body (rebuild_quest_projection_req_t{
                                     "player-bob", "herb-gathering"})
                                   .fetch<quest_progress_t> ();
            ensure (rebuilt.status == "RewardGranted", "rebuild projection failed");

            auto sync = stream.request (sync_quest_progress_req_t{"player-alice"})
                          .async<sync_quest_progress_res_t> ()
                          .result ();
            ensure (sync && any_progress (sync.value ().updated_quests, "first-hunt", 4, ""),
                    "sync failed");
            const auto assertion = http.post ("/self-check/assert")
                                     .body (game_quest_server_assert_req_t{})
                                     .fetch<game_quest_server_assert_res_t> ();
            ensure (assertion.passed, "server evidence failed");
            std::cout << "gamequest-server-evidence=completed\n";
            return true;
        }
        catch (const std::exception &error) {
            std::cerr << "gamequest scenario failed: " << error.what () << "\n";
            return false;
        }
    }

  private:
    static bool contains (const std::vector<std::string> &values, const std::string &needle)
    {
        return std::find (values.begin (), values.end (), needle) != values.end ();
    }

    static bool any_progress (const std::vector<quest_progress_t> &values,
                              const std::string &quest_id,
                              int min_count,
                              const std::string &status)
    {
        return std::any_of (values.begin (), values.end (), [&] (const auto &progress) {
            return progress.quest_id == quest_id && progress.current_count >= min_count
                   && (status.empty () || progress.status == status);
        });
    }

    static void ensure (bool condition, const char *message)
    {
        if (!condition) {
            throw std::runtime_error (message);
        }
    }
};

} // namespace zlink::samples::gamequest
