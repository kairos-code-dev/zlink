/* SPDX-License-Identifier: FSL-1.1-ALv2 */
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
#include <thread>

namespace zlink::samples::gamequest
{

class gamequest_client_scenario_t
{
  public:
    bool run (const std::string &api_a_stream_endpoint,
              const std::string &api_b_stream_endpoint,
              const std::string &api_a_http_url,
              const std::string &api_b_http_url)
    {
        try {
            auto api_a_core = connect (api_a_stream_endpoint);
            auto api_b_core = connect (api_b_stream_endpoint);
            auto api_a = zlink::stream_e2e_client::use (api_a_core);
            auto api_b = zlink::stream_e2e_client::use (api_b_core);

            auto joined = api_a.request (join_session_req_t{"player-alice"})
                            .packet_name (join_session_req_t::packet_name)
                            .async<join_session_res_t> ()
                            .result ();
            dump_initial_join_quests ("player-alice", joined ? joined.value ().active_quests
                                                             : std::vector<quest_progress_t>{});
            ensure (joined && joined.value ().active_quests.empty (),
                    "player-alice initial join should have no active quests");

            auto first_progress = api_a.wait_for<quest_progress_notify_t> ()
                                    .where ([] (const quest_progress_notify_t &notify) {
                                        return notify.player_id == "player-alice"
                                               && notify.progress.quest_id
                                                    == quest_ids_t::first_hunt
                                               && notify.progress.current_count == 1;
                                    })
                                    .timeout (std::chrono::seconds (12))
                                    .to_future ("first hunt progress wait failed");
            auto first_kill = api_a.request (
                                  kill_monster_req_t{"player-alice", "wolf", "forest", "kill-1"})
                                .packet_name (kill_monster_req_t::packet_name)
                                .async<kill_monster_res_t> ()
                                .result ();
            dump_event_id_if_mismatch ("first kill", "player-alice-kill-1", first_kill);
            ensure (first_kill && first_kill.value ().event_id == "player-alice-kill-1",
                    "first kill event id mismatch");
            ensure (first_progress.get ().progress.current_count == 1,
                    "first hunt progress push mismatch");

            auto first_hunt_completed = api_a.wait_for<quest_completed_notify_t> ()
                                          .where ([] (const quest_completed_notify_t &notify) {
                                              return notify.player_id == "player-alice"
                                                     && notify.progress.quest_id
                                                          == quest_ids_t::first_hunt
                                                     && notify.reward_granted;
                                          })
                                          .timeout (std::chrono::seconds (12))
                                          .to_future ("first hunt completion wait failed");
            (void) api_a.request (
                       kill_monster_req_t{"player-alice", "wolf", "forest", "kill-2"})
              .packet_name (kill_monster_req_t::packet_name)
              .async<kill_monster_res_t> ()
              .result ();
            auto third_kill = api_a.request (
                                  kill_monster_req_t{"player-alice", "wolf", "forest", "kill-3"})
                                .packet_name (kill_monster_req_t::packet_name)
                                .async<kill_monster_res_t> ()
                                .result ();
            ensure (third_kill && third_kill.value ().event_id == "player-alice-kill-3",
                    "third kill event id mismatch");
            ensure (first_hunt_completed.get ().progress.status == quest_status_t::reward_granted,
                    "first hunt completion push mismatch");

            auto duplicate = api_a.request (
                                  kill_monster_req_t{"player-alice", "wolf", "forest", "kill-3"})
                               .packet_name (kill_monster_req_t::packet_name)
                               .async<kill_monster_res_t> ()
                               .result ();
            ensure (duplicate && duplicate.value ().event_id == third_kill.value ().event_id,
                    "duplicate kill idempotency mismatch");

            auto auction_completed = api_a.wait_for<quest_completed_notify_t> ()
                                       .where ([] (const quest_completed_notify_t &notify) {
                                           return notify.player_id == "player-alice"
                                                  && notify.progress.quest_id
                                                       == quest_ids_t::open_auction
                                                  && notify.reward_granted;
                                       })
                                       .timeout (std::chrono::seconds (12))
                                       .to_future ("auction quest completion wait failed");
            auto auction =
              api_a.request (unlock_feature_req_t{"player-alice", "auction", "unlock-auction"})
                .packet_name (unlock_feature_req_t::packet_name)
                .async<unlock_feature_res_t> ()
                .result ();
            ensure (auction && auction.value ().event_id == "player-alice-unlock-auction",
                    "auction event id mismatch");
            ensure (auction_completed.get ().progress.status == quest_status_t::reward_granted,
                    "auction completion push mismatch");

            auto offline_item =
              api_a.request (collect_item_req_t{"player-bob", "healing-herb", 1, "herb-1"})
                .packet_name (collect_item_req_t::packet_name)
                .async<collect_item_res_t> ()
                .result ();
            ensure (offline_item && offline_item.value ().event_id == "player-bob-herb-1",
                    "offline herb event id mismatch");

            auto bob_joined = api_b.request (join_session_req_t{"player-bob"})
                                .packet_name (join_session_req_t::packet_name)
                                .async<join_session_res_t> ()
                                .result ();
            ensure (bob_joined
                      && has_progress (bob_joined.value ().active_quests,
                                       quest_ids_t::herb_gathering, 1),
                    "player-bob join did not sync offline herb progress");

            auto herb_completed = api_b.wait_for<quest_completed_notify_t> ()
                                    .where ([] (const quest_completed_notify_t &notify) {
                                        return notify.player_id == "player-bob"
                                               && notify.progress.quest_id
                                                    == quest_ids_t::herb_gathering
                                               && notify.reward_granted;
                                    })
                                    .timeout (std::chrono::seconds (12))
                                    .to_future ("herb completion wait failed");
            auto online_item =
              api_b.request (collect_item_req_t{"player-bob", "healing-herb", 4, "herb-2"})
                .packet_name (collect_item_req_t::packet_name)
                .async<collect_item_res_t> ()
                .result ();
            ensure (online_item && online_item.value ().event_id == "player-bob-herb-2",
                    "online herb event id mismatch");
            ensure (herb_completed.get ().progress.status == quest_status_t::reward_granted,
                    "herb completion push mismatch");

            assert_server (api_a_http_url);
            assert_server (api_b_http_url);
            std::cout << "gamequest-server-evidence=completed\n";
            std::cout << "gamequest=completed\n";
            return true;
        }
        catch (const std::exception &error) {
            std::cerr << "gamequest scenario failed: " << error.what () << "\n";
            return false;
        }
    }

  private:
    using connector_t = zlink::stream_e2e_client::coroutine_connector_t;

    static zlink::stream_connector::connector_t connect (const std::string &endpoint)
    {
        zlink::stream_connector::connector_options_t options;
        options.endpoint = endpoint;
        options.connect_timeout = std::chrono::seconds (5);
        options.request_timeout = std::chrono::seconds (12);
        options.dispatch_mode = zlink::stream_connector::dispatch_mode_t::immediate;
        auto core = zlink::stream_connector::connector_factory_t::create (options);
        auto connected = core.connect ();
        ensure (static_cast<bool> (connected), "stream connect failed");
        return core;
    }

    static bool has_progress (const std::vector<quest_progress_t> &projection,
                              const std::string &quest_id,
                              int current_count)
    {
        return std::any_of (projection.begin (), projection.end (),
                            [&] (const quest_progress_t &progress) {
                                return progress.quest_id == quest_id
                                       && progress.current_count >= current_count;
                            });
    }

    static void dump_initial_join_quests (const std::string &player_id,
                                          const std::vector<quest_progress_t> &quests)
    {
        std::cerr << "gamequest initial join dump player=" << player_id
                  << " activeQuestCount=" << quests.size () << "\n";
        for (const auto &quest : quests) {
            std::cerr << "gamequest initial join quest"
                      << " questId=" << quest.quest_id
                      << " creatorPlayer=" << quest.player_id
                      << " lastEventId=" << quest.last_source_event_id
                      << " updatedAtUnixMs=" << quest.updated_at_unix_ms
                      << " status=" << quest.status
                      << " count=" << quest.current_count << "/"
                      << quest.required_count << "\n";
        }
    }

    template <typename TResult>
    static void dump_event_id_if_mismatch (const char *label,
                                           const std::string &expected,
                                           const TResult &result)
    {
        if (result && result.value ().event_id == expected) {
            return;
        }

        std::cerr << "gamequest event id dump label=" << label << " expected=" << expected;
        if (result) {
            std::cerr << " actual=" << result.value ().event_id;
        }
        else {
            std::cerr << " actual=<no-response>";
            if (result.error ()) {
                std::cerr << " error=" << result.error ()->message;
            }
        }
        std::cerr << "\n";
    }

    static void assert_server (const std::string &api_http_url)
    {
        auto assertion = zlink::http_client::client_t::create (api_http_url)
                           .timeout (std::chrono::seconds (12))
                           .build ()
                           .post ("/self-check/assert")
                           .body (server_assertion_req_t{})
                           .fetch<server_assertion_res_t> ();
        ensure (assertion.passed, "server assertion failed");
    }

    static void ensure (bool condition, const char *message)
    {
        if (!condition) {
            throw std::runtime_error (message);
        }
    }
};

} // namespace zlink::samples::gamequest
