/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "bingo_client_options.hpp"
#include "../Shared/Contracts/messages.hpp"

#include <zlink/stream_e2e_client/codecs/auto_codec.hpp>

#include <algorithm>
#include <chrono>
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
              stream_e2e_client::coroutine_connector_t &client2)
    {
        auto result = run_async (client1, client2).result ();
        return result && result.value ();
    }

#undef ensure
  private:
    static stream_e2e_client::task_t<bool>
    run_async (stream_e2e_client::coroutine_connector_t &client1,
               stream_e2e_client::coroutine_connector_t &client2)
    {
        try {
            const std::vector<int> client1_card_numbers{1, 2, 3, 4, 0, 6, 7, 8, 9};
            const std::vector<int> client2_card_numbers{10, 11, 12, 13, 0, 14, 4, 5, 6};

            trace ("connect client1");
            co_await client1.connect ().async ();
            trace ("connect client2");
            co_await client2.connect ().async ();

            trace ("authenticate client1");
            auto client1_auth =
              co_await client1.request (authenticate_req_t{bingo_sample_players_t::player1})
                .async<authenticate_res_t> ();
            ensure (client1_auth.actor_id == bingo_sample_players_t::player1);

            trace ("match client1");
            auto client1_match =
              co_await client1.request (match_bingo_req_t{bingo_sample_modes_t::two_player})
                .async<match_bingo_res_t> ();
            ensure (client1_match.state.status == bingo_room_status_t::waiting);
            ensure (client1_match.state.host_actor_id == client1_auth.actor_id);
            auto client1_self_join =
              client1.wait_for<player_joined_notify_t> ()
                .where (&player_joined_notify_t::actor_id, client1_auth.actor_id)
                .timeout (std::chrono::milliseconds (25))
                .async ()
                .result ();
            ensure (!client1_self_join, "self join notify must not be delivered");

            trace ("authenticate client2");
            auto client2_auth =
              co_await client2.request (authenticate_req_t{bingo_sample_players_t::player2})
                .async<authenticate_res_t> ();
            ensure (client2_auth.actor_id == bingo_sample_players_t::player2);
            ensure (client2_auth.actor_id != client1_auth.actor_id);

            trace ("match client2");
            auto client2_match =
              co_await client2.request (match_bingo_req_t{bingo_sample_modes_t::two_player})
                .async<match_bingo_res_t> ();
            ensure (client2_match.room_id == client1_match.room_id);
            ensure (client2_match.state.status == bingo_room_status_t::running);
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
            auto client1_card = co_await client1
                                  .request (submit_bingo_card_req_t{room_id, client1_card_numbers})
                                  .async<submit_bingo_card_res_t> ();
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

            co_await client1.close ().async ();
            co_await client2.close ().async ();
            co_return true;
        }
        catch (const std::exception &ex) {
            std::cerr << "bingo game failed: " << ex.what () << '\n';
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

    static void trace (const char *step)
    {
        std::cerr << "bingo step: " << step << '\n';
    }
};

} // namespace zlink::samples::bingo
