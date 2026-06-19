/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "bingo_client_options.hpp"
#include "../Shared/Contracts/messages.hpp"

#include <zlink/stream_e2e_client/codecs/auto_codec.hpp>

#include <algorithm>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#define ensure(condition) ensure ((condition), #condition)

namespace zlink::samples::bingo
{

class bingo_client_scenario_t
{
  public:
    bool run (stream_e2e_client::coroutine_connector_t &client1,
              stream_e2e_client::coroutine_connector_t &client2,
              stream_e2e_client::coroutine_connector_t &observer)
    {
        auto result = run_async (client1, client2, observer).result ();
        return result && result.value ();
    }

#undef ensure
  private:
    static stream_e2e_client::task_t<bool>
    run_async (stream_e2e_client::coroutine_connector_t &client1,
               stream_e2e_client::coroutine_connector_t &client2,
               stream_e2e_client::coroutine_connector_t &observer)
    {
        try {
            const std::vector<int> client1_card_numbers{1, 2, 3, 4, 0, 6, 7, 8, 9};
            const std::vector<int> client2_card_numbers{10, 11, 12, 13, 0, 14, 4, 5, 6};

            trace ("connect client1");
            co_await client1.connect ().async ();
            trace ("connect client2");
            co_await client2.connect ().async ();
            trace ("connect observer");
            co_await observer.connect ().async ();

            trace ("authenticate client1");
            auto client1_auth =
              co_await client1.request (authenticate_req_t{bingo_sample_players_t::player1})
                .async<authenticate_res_t> ();
            ensure (client1_auth.actor_id == bingo_sample_players_t::player1);
            ensure (!client1_auth.actor_node_rid.empty ());

            trace ("match client1");
            auto client1_match =
              co_await client1.request (match_bingo_req_t{bingo_sample_modes_t::two_player})
                .async<match_bingo_res_t> ();
            ensure (client1_match.state.status == bingo_room_status_t::waiting);
            ensure (client1_match.state.host_actor_id == client1_auth.actor_id);
            ensure (client1_match.room_owner_node_rid == client1_auth.actor_node_rid);
            auto client1_self_join =
              client1.wait_for<player_joined_notify_t> ()
                .where (&player_joined_notify_t::actor_id, client1_auth.actor_id)
                .timeout (std::chrono::milliseconds (25))
                .async ()
                .result ();
            ensure (!client1_self_join, "self join notify must not be delivered");

            trace ("authenticate observer");
            auto observer_auth =
              co_await observer.request (authenticate_req_t{bingo_sample_players_t::observer})
                .async<authenticate_res_t> ();
            ensure (observer_auth.actor_id == bingo_sample_players_t::observer);
            ensure (observer_auth.actor_node_rid != client1_match.room_owner_node_rid);

            trace ("observe reward events");
            auto observed =
              co_await observer.request (observe_bingo_events_req_t{client1_match.room_id})
                .async<observe_bingo_events_res_t> ();
            ensure (observed.subscribed);
            ensure (observed.observer_node_rid == observer_auth.actor_node_rid);
            ensure (observed.observer_node_rid != client1_match.room_owner_node_rid);

            trace ("authenticate client2");
            auto client2_auth =
              co_await client2.request (authenticate_req_t{bingo_sample_players_t::player2})
                .async<authenticate_res_t> ();
            ensure (client2_auth.actor_id == bingo_sample_players_t::player2);
            ensure (client2_auth.actor_id != client1_auth.actor_id);
            ensure (client2_auth.actor_node_rid != client1_auth.actor_node_rid);

            trace ("match client2");
            auto client1_joined_future = std::async (std::launch::async, [&client1, &client2_auth] {
                auto result =
                  client1.wait_for<player_joined_notify_t> ()
                    .where (&player_joined_notify_t::actor_id, client2_auth.actor_id)
                    .async ()
                    .result ();
                if (!result) {
                    throw std::runtime_error ("client1 joined notify wait failed");
                }
                return result.value ();
            });
            auto client1_started_future = std::async (std::launch::async, [&client1] {
                auto result = client1.wait_for<game_started_notify_t> ().async ().result ();
                if (!result) {
                    throw std::runtime_error ("client1 game started wait failed");
                }
                return result.value ();
            });
            auto client2_match_future = std::async (std::launch::async, [&client2] {
                auto result = client2.request (match_bingo_req_t{bingo_sample_modes_t::two_player})
                                .async<match_bingo_res_t> ()
                                .result ();
                if (!result) {
                    throw std::runtime_error ("client2 match failed");
                }
                return result.value ();
            });

            auto client2_match = client2_match_future.get ();
            ensure (client2_match.room_id == client1_match.room_id);
            ensure (client2_match.state.status == bingo_room_status_t::running);
            ensure (client2_match.room_owner_node_rid == client1_match.room_owner_node_rid);
            ensure (client2_auth.actor_node_rid != client2_match.room_owner_node_rid);
            auto client1_joined = client1_joined_future.get ();
            auto client1_started = client1_started_future.get ();
            auto client2_self_join =
              client2.wait_for<player_joined_notify_t> ()
                .where (&player_joined_notify_t::actor_id, client2_auth.actor_id)
                .timeout (std::chrono::milliseconds (25))
                .async ()
                .result ();
            ensure (!client2_self_join, "self join notify must not be delivered");
            ensure (std::any_of (
              client2_match.state.players.begin (), client2_match.state.players.end (),
              [&client1_auth] (const bingo_player_state_t &player) {
                  return player.actor_id == client1_auth.actor_id && player.is_host;
              }));
            ensure (std::any_of (
              client2_match.state.players.begin (), client2_match.state.players.end (),
              [&client2_auth] (const bingo_player_state_t &player) {
                  return player.actor_id == client2_auth.actor_id && !player.is_host;
              }));

            const auto room_id = client1_match.room_id;
            trace ("client1 wait joined");
            ensure (client1_joined.actor_id == client2_auth.actor_id);

            trace ("wait game started");
            ensure (client1_started.state.room_id == room_id);
            ensure (client2_match.state.room_id == room_id);

            trace ("client2 submit card");
            auto client2_card = co_await client2
                                  .request (submit_bingo_card_req_t{room_id, client2_card_numbers})
                                  .async<submit_bingo_card_res_t> ();
            ensure (client2_card.state.status == bingo_room_status_t::running);
            ensure (std::any_of (
              client2_card.state.players.begin (), client2_card.state.players.end (),
              [&client2_auth] (const bingo_player_state_t &player) {
                  return player.actor_id == client2_auth.actor_id && player.card.size () == 9;
              }));

            trace ("client1 submit card");
            auto reward_future = std::async (std::launch::async, [&observer, &room_id] {
                auto result =
                  observer.wait_for<bingo_reward_announced_notify_t> ()
                    .where (&bingo_reward_announced_notify_t::room_id, room_id)
                    .async ()
                    .result ();
                if (!result) {
                    throw std::runtime_error ("reward announcement wait failed");
                }
                return result.value ();
            });
            auto client1_card =
              co_await client1.request (submit_bingo_card_req_t{room_id, client1_card_numbers})
                .async<submit_bingo_card_res_t> ();
            trace ("wait reward announcement");
            auto reward = reward_future.get ();
            ensure (client1_card.state.status == bingo_room_status_t::finished);
            ensure (std::all_of (
              client1_card.state.players.begin (), client1_card.state.players.end (),
              [] (const bingo_player_state_t &player) { return player.card.size () == 9; }));
            ensure (!client1_card.state.drawn_numbers.empty ());
            ensure (client1_card.state.winners == std::vector<std::string>{client1_auth.actor_id});
            ensure (std::all_of (
              client1_card.state.players.begin (), client1_card.state.players.end (),
              [] (const bingo_player_state_t &player) {
                  return player.card.size () == 9 && player.marks.size () == 9 && player.marks[4];
              }));

            ensure (reward.actor_id == client1_auth.actor_id);
            ensure (reward.item_id == bingo_reward_items_t::golden_dauber_id);
            ensure (reward.item_name == bingo_reward_items_t::golden_dauber_name);
            ensure (reward.rarity == bingo_reward_items_t::legendary_rarity);
            ensure (reward.receiving_spot_node_rid == observed.observer_node_rid);

            trace ("stop observing");
            auto stopped =
              co_await observer.request (stop_observing_bingo_events_req_t{room_id})
                .async<stop_observing_bingo_events_res_t> ();
            ensure (stopped.stopped);
            ensure (stopped.observer_node_rid == observed.observer_node_rid);

            co_await client1.close ().async ();
            co_await client2.close ().async ();
            co_await observer.close ().async ();
            co_return true;
        }
        catch (const std::exception &ex) {
            std::cerr << "bingo game failed: " << ex.what () << '\n';
            (void) client1.close ();
            (void) client2.close ();
            (void) observer.close ();
            co_return false;
        }
    }

    static void ensure (bool condition, const char *expression)
    {
        if (!condition) { throw std::runtime_error (std::string ("Ensure failed: ") + expression); }
    }

    static void ensure (bool condition)
    {
        ensure (condition, "condition");
    }

    static void trace (const char *step)
    {
        std::cerr << "bingo step: " << step << '\n';
    }
};

} // namespace zlink::samples::bingo
