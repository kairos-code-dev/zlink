/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "bingo_client_options.hpp"
#include "../Shared/Contracts/messages.hpp"
#include "../Shared/Contracts/protobuf_stream_codec.hpp"

#include <zlink/stream_e2e_client/codecs/auto_codec.hpp>

#include <algorithm>
#include <chrono>
#include <future>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
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

            /* 공통 sample spec §10-1: 세 client(player-1/player-2/observer)를 먼저 인증하고
             * player-1과 player-2의 actor node rid가 서로 다른지 확인한 뒤 matching으로 넘어간다. */
            trace ("authenticate client1");
            const auto client1_auth_request = authenticate_req_t{bingo_sample_players_t::player1};
            auto client1_auth = co_await authenticate (client1, client1_auth_request);
            ensure (client1_auth.actor_id == bingo_sample_players_t::player1);
            ensure (!client1_auth.actor_node_rid.empty ());

            trace ("authenticate client2");
            const auto client2_auth_request = authenticate_req_t{bingo_sample_players_t::player2};
            auto client2_auth = co_await authenticate (client2, client2_auth_request);
            ensure (client2_auth.actor_id == bingo_sample_players_t::player2);
            ensure (client2_auth.actor_id != client1_auth.actor_id);
            ensure (client2_auth.actor_node_rid != client1_auth.actor_node_rid);

            trace ("authenticate observer");
            const auto observer_auth_request = authenticate_req_t{bingo_sample_players_t::observer};
            auto observer_auth = co_await authenticate (observer, observer_auth_request);
            ensure (observer_auth.actor_id == bingo_sample_players_t::observer);

            trace ("match client1");
            const auto client1_match_request = match_bingo_req_t{bingo_sample_modes_t::two_player};
            auto client1_match =
              co_await client1.request (client1_match_request)
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

            ensure (observer_auth.actor_node_rid != client1_match.room_owner_node_rid);

            trace ("observe reward events");
            const auto observe_request = observe_bingo_events_req_t{client1_match.room_id};
            auto observed = co_await observer.request (observe_request)
                              .async<observe_bingo_events_res_t> ();
            ensure (observed.subscribed);
            ensure (observed.observer_node_rid == observer_auth.actor_node_rid);
            ensure (observed.observer_node_rid != client1_match.room_owner_node_rid);

            trace ("match client2");
            auto client1_joined_future =
              client1.wait_for<player_joined_notify_t> ()
                .where (&player_joined_notify_t::actor_id, client2_auth.actor_id)
                .to_future ("client1 joined notify wait failed");
            auto client1_started_future = client1.wait_for<game_started_notify_t> ().to_future (
              "client1 game started wait failed");
            auto client2_started_future = client2.wait_for<game_started_notify_t> ().to_future (
              "client2 game started wait failed");
            const auto match_request = match_bingo_req_t{bingo_sample_modes_t::two_player};
            auto client2_match =
              co_await client2.request (match_request).async<match_bingo_res_t> ();
            ensure (client2_match.room_id == client1_match.room_id);
            ensure (client2_match.state.status == bingo_room_status_t::running);
            ensure (client2_match.room_owner_node_rid == client1_match.room_owner_node_rid);
            ensure (client2_auth.actor_node_rid != client2_match.room_owner_node_rid);
            auto client1_joined = client1_joined_future.get ();
            auto client1_started = client1_started_future.get ();
            auto client2_started = client2_started_future.get ();
            auto client2_self_join =
              client2.wait_for<player_joined_notify_t> ()
                .where (&player_joined_notify_t::actor_id, client2_auth.actor_id)
                .timeout (std::chrono::milliseconds (25))
                .async ()
                .result ();
            ensure (!client2_self_join, "self join notify must not be delivered");
            ensure (
              std::any_of (client2_match.state.players.begin (), client2_match.state.players.end (),
                           [&client1_auth] (const bingo_player_state_t &player) {
                               return player.actor_id == client1_auth.actor_id && player.is_host;
                           }));
            ensure (
              std::any_of (client2_match.state.players.begin (), client2_match.state.players.end (),
                           [&client2_auth] (const bingo_player_state_t &player) {
                               return player.actor_id == client2_auth.actor_id && !player.is_host;
                           }));

            const auto room_id = client1_match.room_id;
            trace ("client1 wait joined");
            ensure (client1_joined.actor_id == client2_auth.actor_id);

            trace ("wait game started");
            ensure (client1_started.state.room_id == room_id);
            ensure (client1_started.state.status == bingo_room_status_t::running);
            ensure (client2_started.state.room_id == room_id);
            ensure (client2_started.state.status == bingo_room_status_t::running);
            ensure (client2_match.state.room_id == room_id);

            bool player_stop_observing_rejected = false;
            try {
                (void) co_await client1.request (stop_observing_bingo_events_req_t{room_id})
                  .async<stop_observing_bingo_events_res_t> ();
            }
            catch (const std::exception &) {
                player_stop_observing_rejected = true;
            }
            ensure (player_stop_observing_rejected,
                    "a game-room player must not stop an observer subscription");

            trace ("client2 submit card");
            const auto client2_card_request =
              submit_bingo_card_req_t{room_id, client2_card_numbers};
            auto client2_card =
              co_await client2.request (client2_card_request)
                .async<submit_bingo_card_res_t> ();
            ensure (client2_card.state.status == bingo_room_status_t::running);
            ensure (std::any_of (
              client2_card.state.players.begin (), client2_card.state.players.end (),
              [&client2_auth] (const bingo_player_state_t &player) {
                  return player.actor_id == client2_auth.actor_id && player.card.size () == 9;
              }));

            bool duplicate_card_rejected = false;
            try {
                (void) co_await client2.request (client2_card_request)
                  .async<submit_bingo_card_res_t> ();
            }
            catch (const std::exception &) {
                duplicate_card_rejected = true;
            }
            ensure (duplicate_card_rejected, "a player must not replace a submitted card");

            trace ("client1 submit card");
            constexpr int expected_draw_count = 3;
            auto reward_future = observer.wait_for<bingo_reward_announced_notify_t> ()
                                   .where (&bingo_reward_announced_notify_t::room_id, room_id)
                                   .to_future ("reward announcement wait failed");
            std::vector<std::future<number_drawn_notify_t>> client1_draw_futures;
            std::vector<std::future<number_drawn_notify_t>> client2_draw_futures;
            client1_draw_futures.reserve (expected_draw_count);
            client2_draw_futures.reserve (expected_draw_count);
            for (int draw_seq = 1; draw_seq <= expected_draw_count; ++draw_seq) {
                client1_draw_futures.push_back (
                  client1.wait_for<number_drawn_notify_t> ()
                    .where (&number_drawn_notify_t::draw_seq, draw_seq)
                    .to_future ("client1 draw notify wait failed"));
                client2_draw_futures.push_back (
                  client2.wait_for<number_drawn_notify_t> ()
                    .where (&number_drawn_notify_t::draw_seq, draw_seq)
                    .to_future ("client2 draw notify wait failed"));
            }
            auto client1_ended_future =
              client1.wait_for<game_ended_notify_t> ()
                .where ([] (const game_ended_notify_t &message) {
                    return message.state.status == bingo_room_status_t::finished;
                })
                .to_future ("client1 game ended wait failed");
            auto client2_ended_future =
              client2.wait_for<game_ended_notify_t> ()
                .where ([] (const game_ended_notify_t &message) {
                    return message.state.status == bingo_room_status_t::finished;
                })
                .to_future ("client2 game ended wait failed");
            const auto client1_card_request =
              submit_bingo_card_req_t{room_id, client1_card_numbers};
            auto client1_card =
              co_await client1.request (client1_card_request)
                .async<submit_bingo_card_res_t> ();
            // Drawing is server-driven after both cards arrive; the submit reply
            // still reflects the running game (same as the .NET scenario).
            ensure (client1_card.state.status == bingo_room_status_t::running);
            std::vector<number_drawn_notify_t> drawn_numbers;
            for (int draw_seq = 1; draw_seq <= expected_draw_count; ++draw_seq) {
                auto client1_drawn =
                  client1_draw_futures[static_cast<std::size_t> (draw_seq - 1)].get ();
                auto client2_drawn =
                  client2_draw_futures[static_cast<std::size_t> (draw_seq - 1)].get ();
                drawn_numbers.push_back (client1_drawn);
                ensure (client1_drawn.draw_seq == draw_seq);
                ensure (client2_drawn.draw_seq == draw_seq);
                ensure (client2_drawn.number == client1_drawn.number);
            }
            ensure (drawn_numbers.size () == expected_draw_count);
            ensure (drawn_numbers.back ().state.status == bingo_room_status_t::finished);
            auto client1_ended = client1_ended_future.get ();
            auto client2_ended = client2_ended_future.get ();
            ensure (client1_ended.state.status == bingo_room_status_t::finished);
            ensure (client2_ended.state.status == bingo_room_status_t::finished);
            ensure (client2_ended.state.drawn_numbers == client1_ended.state.drawn_numbers);
            ensure (client2_ended.state.winners == client1_ended.state.winners);
            ensure (client1_ended.state.drawn_numbers.size () == drawn_numbers.size ());
            for (std::size_t index = 0; index < drawn_numbers.size (); ++index) {
                ensure (client1_ended.state.drawn_numbers[index] == drawn_numbers[index].number);
            }
            // Final results are validated on the pushed game-ended state, matching
            // the .NET scenario: winners, full cards, and the marked free cell.
            ensure (!client1_ended.state.drawn_numbers.empty ());
            ensure (client1_ended.state.winners
                    == std::vector<std::string>{client1_auth.actor_id});
            ensure (std::all_of (
              client1_ended.state.players.begin (), client1_ended.state.players.end (),
              [] (const bingo_player_state_t &player) {
                  return player.card.size () == 9 && player.marks.size () == 9 && player.marks[4];
              }));

            trace ("wait reward announcement");
            auto reward = reward_future.get ();
            ensure (reward.actor_id == client1_auth.actor_id);
            ensure (reward.draw_seq == client1_ended.state.draw_seq);
            ensure (reward.item_id == bingo_reward_items_t::golden_dauber_id);
            ensure (reward.item_name == bingo_reward_items_t::golden_dauber_name);
            ensure (reward.rarity == bingo_reward_items_t::legendary_rarity);
            ensure (reward.receiving_spot_node_rid == observed.observer_node_rid);

            trace ("stop observing");
            const auto stop_observing_request = stop_observing_bingo_events_req_t{room_id};
            auto stopped =
              co_await observer.request (stop_observing_request)
                .async<stop_observing_bingo_events_res_t> ();
            trace ("stop observing completed");
            ensure (stopped.stopped);
            ensure (stopped.observer_node_rid == observed.observer_node_rid);

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
        if (!condition) {
            throw std::runtime_error (std::string ("Ensure failed: ") + expression);
        }
    }

    static stream_e2e_client::task_t<authenticate_res_t>
    authenticate (stream_e2e_client::coroutine_connector_t &client,
                  const authenticate_req_t &request)
    {
        co_return co_await client.request (request).async<authenticate_res_t> ();
    }

    static void ensure (bool condition,
                        std::source_location location = std::source_location::current ())
    {
        ensure (condition, ("condition at " + std::string (location.file_name ()) + ":"
                            + std::to_string (location.line ()))
                             .c_str ());
    }

    static void trace (const char *step) { std::cerr << "bingo step: " << step << '\n'; }
};

} // namespace zlink::samples::bingo
