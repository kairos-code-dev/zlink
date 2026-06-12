/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "bingo_client_options.hpp"
#include "../Shared/Contracts/messages.hpp"

#include <zlink/stream_e2e_client/codecs/auto_codec.hpp>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

#define ensure(condition) ensure ((condition), #condition)
#define ensure_result(result) ensure_result ((result), #result)

namespace zlink::samples::bingo
{

class bingo_client_scenario_t
{
  public:
    bool run (stream_e2e_client::coroutine_connector_t &client1,
              stream_e2e_client::coroutine_connector_t &client2)
    {
        auto result = run_async (client1, client2).result ();
        return result && result.value ();
    }

#undef ensure
#undef ensure_result

  private:
    static stream_e2e_client::task_t<bool>
    run_async (stream_e2e_client::coroutine_connector_t &client1,
               stream_e2e_client::coroutine_connector_t &client2)
    {
        try {
            ensure (co_await client1.connect ().async ());
            ensure (co_await client2.connect ().async ());

            auto client1_auth = ensure_result (
              co_await client1.request<authenticate_res_t> (
                authenticate_req_t{"player-1"}).async ());
            ensure (client1_auth.actor_id == "player-1");

            auto client1_match = ensure_result (
              co_await client1.request<match_bingo_res_t> (
                match_bingo_req_t{"two-player"}).async ());
            ensure (client1_match.state.status == "WaitingForPlayers");
            ensure (client1_match.state.host_actor_id == client1_auth.actor_id);
            ensure (co_await ensure_no_self_join_notify (client1, client1_auth.actor_id));

            auto client2_auth = ensure_result (
              co_await client2.request<authenticate_res_t> (
                authenticate_req_t{"player-2"}).async ());
            ensure (client2_auth.actor_id == "player-2");
            ensure (client2_auth.actor_id != client1_auth.actor_id);

            auto client1_wait_client2_join = client1.wait_for<player_joined_notify_t> ().async ();
            auto client1_wait_started = client1.wait_for<game_started_notify_t> ().async ();
            auto client2_wait_started = client2.wait_for<game_started_notify_t> ().async ();
            client1_wait_client2_join.start ();
            client1_wait_started.start ();
            client2_wait_started.start ();
            auto client2_match = ensure_result (
              co_await client2.request<match_bingo_res_t> (
                match_bingo_req_t{"two-player"}).async ());
            ensure (client2_match.room_id == client1_match.room_id);
            ensure (client2_match.state.status == "Running");
            ensure (co_await ensure_no_self_join_notify (client2, client2_auth.actor_id));

            auto client1_saw_client2_join = ensure_result (
              co_await client1_wait_client2_join);
            ensure (client1_saw_client2_join.actor_id == client2_auth.actor_id);
            ensure (client1_saw_client2_join.room_id == client1_match.room_id);

            auto client1_started = ensure_result (co_await client1_wait_started);
            auto client2_started = ensure_result (co_await client2_wait_started);
            ensure (client1_started.state.status == "Running");
            ensure (client2_started.state.status == "Running");

            const auto room_id = client1_match.room_id;
            auto client2_card_req =
              submit_bingo_card_req_t{room_id, {10, 11, 12, 13, 0, 14, 4, 5, 6}};
            auto client2_card = ensure_result (
              co_await client2.request<submit_bingo_card_res_t> (client2_card_req).async ());
            ensure (client2_card.state.status == "Running");
            ensure (std::any_of (
              client2_card.state.players.begin (), client2_card.state.players.end (),
              [&client2_auth] (const bingo_player_state_t &player) {
                  return player.actor_id == client2_auth.actor_id && player.card.size () == 9;
              }));

            auto client1_wait_first_draw =
              wait_for_draw (client1, 1);
            auto client2_wait_first_draw =
              wait_for_draw (client2, 1);
            auto client1_wait_result = client1.wait_for<game_ended_notify_t> ().async ();
            auto client2_wait_result = client2.wait_for<game_ended_notify_t> ().async ();
            client1_wait_first_draw.start ();
            client2_wait_first_draw.start ();
            client1_wait_result.start ();
            client2_wait_result.start ();
            auto client1_card_req =
              submit_bingo_card_req_t{room_id, {1, 2, 3, 4, 0, 6, 7, 8, 9}};
            auto client1_card = ensure_result (
              co_await client1.request<submit_bingo_card_res_t> (client1_card_req).async ());
            ensure (client1_card.state.status == "Running");
            ensure (std::all_of (
              client1_card.state.players.begin (), client1_card.state.players.end (),
              [] (const bingo_player_state_t &player) { return player.card.size () == 9; }));

            std::vector<int> drawn_numbers;
            auto client1_drawn = ensure_result (co_await client1_wait_first_draw);
            auto client2_drawn = ensure_result (co_await client2_wait_first_draw);
            for (;;) {
                const auto expected_seq = static_cast<int> (drawn_numbers.size ()) + 1;
                ensure (client1_drawn.draw_seq == expected_seq);
                ensure (client2_drawn.draw_seq == expected_seq);
                ensure (client2_drawn.draw_seq == client1_drawn.draw_seq);
                ensure (client2_drawn.number == client1_drawn.number);

                drawn_numbers.push_back (client1_drawn.number);
                if (client1_drawn.state.status == "Finished") {
                    break;
                }

                const auto next_seq = expected_seq + 1;
                auto client1_next_draw = wait_for_draw (client1, next_seq);
                auto client2_next_draw = wait_for_draw (client2, next_seq);
                client1_next_draw.start ();
                client2_next_draw.start ();
                client1_drawn = ensure_result (co_await client1_next_draw);
                client2_drawn = ensure_result (co_await client2_next_draw);
            }
            ensure (!drawn_numbers.empty ());

            auto client1_result = ensure_result (co_await client1_wait_result);
            auto client2_result = ensure_result (co_await client2_wait_result);
            ensure (client1_result.state.status == "Finished");
            ensure (client2_result.state.status == "Finished");
            ensure (client2_result.state.drawn_numbers == client1_result.state.drawn_numbers);
            ensure (client2_result.state.winners == client1_result.state.winners);
            ensure (client1_result.state.drawn_numbers == drawn_numbers);
            ensure (client1_result.state.winners == std::vector<std::string>{client1_auth.actor_id});
            ensure (std::all_of (
              client1_result.state.players.begin (), client1_result.state.players.end (),
              [] (const bingo_player_state_t &player) {
                  return player.card.size () == 9 && player.marks.size () == 9 && player.marks[4];
              }));

            (void) co_await client1.close ().async ();
            (void) co_await client2.close ().async ();
            co_return true;
        }
        catch (const std::exception &ex) {
            (void) ex;
            (void) client1.close ();
            (void) client2.close ();
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

    static void ensure (stream_e2e_client::result_t<void> result)
    {
        if (!result) {
            const auto message =
              result.error () ? result.error ()->message : "operation failed";
            throw std::runtime_error (std::string ("Ensure failed: ") + message);
        }
    }

    template <typename T>
    static T ensure_result (stream_e2e_client::result_t<T> result, const char *expression)
    {
        if (!result) {
            const auto message =
              result.error () ? result.error ()->message : "operation failed";
            throw std::runtime_error (std::string ("Ensure failed: ") + expression
                                      + ": " + message);
        }
        return result.value ();
    }

    template <typename T>
    static T ensure_result (stream_e2e_client::result_t<T> result)
    {
        return ensure_result (std::move (result), "operation");
    }

    static stream_e2e_client::task_t<number_drawn_notify_t>
    wait_for_draw (stream_e2e_client::coroutine_connector_t &client, int expected_seq)
    {
        auto drawn = co_await client.wait_for<number_drawn_notify_t> ()
                       .where ([expected_seq] (const number_drawn_notify_t &message) {
                           return message.draw_seq == expected_seq;
                       })
                       .async ();
        co_return drawn;
    }

    static stream_e2e_client::task_t<void>
    ensure_no_self_join_notify (stream_e2e_client::coroutine_connector_t &client,
                                const std::string &actor_id)
    {
        auto notify =
          co_await client.wait_for<player_joined_notify_t> ()
            .where ([actor_id] (const player_joined_notify_t &message) {
                return message.actor_id == actor_id;
            })
            .timeout (std::chrono::milliseconds (25))
            .async ();
        ensure (!notify, "self join notify must not be delivered");
        co_return;
    }
};

inline void register_bingo_client_codecs (stream_connector::connector_t &connector)
{
    connector.codecs ()
      .add_protobuf<authenticate_req_t> ()
      .add_protobuf<authenticate_res_t> ()
      .add_protobuf<match_bingo_req_t> ()
      .add_protobuf<match_bingo_res_t> ()
      .add_protobuf<submit_bingo_card_req_t> ()
      .add_protobuf<submit_bingo_card_res_t> ()
      .add_protobuf<player_joined_notify_t> ()
      .add_protobuf<game_started_notify_t> ()
      .add_protobuf<number_drawn_notify_t> ()
      .add_protobuf<game_ended_notify_t> ();
}

} // namespace zlink::samples::bingo
