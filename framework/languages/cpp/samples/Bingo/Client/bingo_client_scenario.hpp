/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "bingo_client_options.hpp"
#include "../Shared/Contracts/messages.hpp"

#include <zlink/stream_connector/codecs/auto_codec.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#define ensure(condition) ensure ((condition), #condition)
#define ensure_result(result) ensure_result ((result), #result)

namespace zlink::samples::bingo
{

inline constexpr const char *bingo_client_log_file = "bingo-client.log";

class bingo_client_scenario_t
{
  public:
    bool run (stream_connector::connector_t &client1,
              stream_connector::connector_t &client2)
    {
        std::ofstream log (bingo_client_log_file, std::ios::trunc);

        try {
            auto client1_connect = client1.connect ();
            auto client2_connect = client2.connect ();
            ensure (client1_connect && client2_connect);

            auto client1_auth = ensure_result (client1.request<authenticate_res_t> (authenticate_req_t{"player-1"}).submit ());
            ensure (client1_auth.actor_id == "player-1");

            auto client1_match = ensure_result (client1.request<match_bingo_res_t> (match_bingo_req_t{"two-player"}).submit ());
            ensure (client1_match.state.status == "WaitingForPlayers");
            ensure (client1_match.state.host_actor_id == client1_auth.actor_id);
            ensure_no_self_join_notify (client1, client1_auth.actor_id);

            auto client2_auth = ensure_result (client2.request<authenticate_res_t> (authenticate_req_t{"player-2"}).submit ());
            ensure (client2_auth.actor_id == "player-2");
            ensure (client2_auth.actor_id != client1_auth.actor_id);

            auto client2_match = ensure_result (client2.request<match_bingo_res_t> (match_bingo_req_t{"two-player"}).submit ());
            ensure (client2_match.room_id == client1_match.room_id);
            ensure (client2_match.state.status == "Running");
            ensure_no_self_join_notify (client2, client2_auth.actor_id);

            auto client1_saw_client2_join = ensure_result (client1.wait_for<player_joined_notify_t> ().submit ());
            ensure (client1_saw_client2_join.actor_id == client2_auth.actor_id);
            ensure (client1_saw_client2_join.room_id == client1_match.room_id);

            auto client1_started = ensure_result (client1.wait_for<game_started_notify_t> ().submit ());
            auto client2_started = ensure_result (client2.wait_for<game_started_notify_t> ().submit ());
            ensure (client1_started.state.status == "Running");
            ensure (client2_started.state.status == "Running");

            const auto room_id = client1_match.room_id;
            auto client2_card_req = submit_bingo_card_req_t{room_id, {10, 11, 12, 13, 0, 14, 4, 5, 6}};
            auto client2_card = ensure_result (client2.request<submit_bingo_card_res_t> (client2_card_req).submit ());
            ensure (client2_card.state.status == "Running");
            ensure (std::any_of (client2_card.state.players.begin (), client2_card.state.players.end (), [&client2_auth] (const bingo_player_state_t &player) { return player.actor_id == client2_auth.actor_id && player.card.size () == 9; }));

            auto client1_card_req = submit_bingo_card_req_t{room_id, {1, 2, 3, 4, 0, 6, 7, 8, 9}};
            auto client1_card = ensure_result (client1.request<submit_bingo_card_res_t> (client1_card_req).submit ());
            ensure (client1_card.state.status == "Running");
            ensure (std::all_of (client1_card.state.players.begin (), client1_card.state.players.end (), [] (const bingo_player_state_t &player) { return player.card.size () == 9; }));

            std::vector<int> drawn_numbers;
            while (true) {
                const auto expected_seq = static_cast<int> (drawn_numbers.size ()) + 1;
                auto client1_drawn = ensure_result (client1.wait_for<number_drawn_notify_t> ()
                                                      .where ([expected_seq] (const number_drawn_notify_t &message) {
                                                          return message.draw_seq == expected_seq;
                                                      })
                                                      .submit ());
                auto client2_drawn = ensure_result (client2.wait_for<number_drawn_notify_t> ()
                                                      .where ([expected_seq] (const number_drawn_notify_t &message) {
                                                          return message.draw_seq == expected_seq;
                                                      })
                                                      .submit ());
                ensure (client1_drawn.draw_seq == expected_seq);
                ensure (client2_drawn.draw_seq == expected_seq);
                ensure (client2_drawn.draw_seq == client1_drawn.draw_seq);
                ensure (client2_drawn.number == client1_drawn.number);

                drawn_numbers.push_back (client1_drawn.number);
                if (client1_drawn.state.status == "Finished") {
                    break;
                }
            }
            ensure (!drawn_numbers.empty ());

            auto client1_result = ensure_result (client1.wait_for<game_ended_notify_t> ().submit ());
            auto client2_result = ensure_result (client2.wait_for<game_ended_notify_t> ().submit ());
            ensure (client1_result.state.status == "Finished");
            ensure (client2_result.state.status == "Finished");
            ensure (client2_result.state.drawn_numbers == client1_result.state.drawn_numbers);
            ensure (client2_result.state.winners == client1_result.state.winners);
            ensure (client1_result.state.drawn_numbers == drawn_numbers);
            ensure (client1_result.state.winners == std::vector<std::string>{client1_auth.actor_id});
            ensure (std::all_of (client1_result.state.players.begin (), client1_result.state.players.end (), [] (const bingo_player_state_t &player) { return player.card.size () == 9 && player.marks.size () == 9 && player.marks[4]; }));

            log << "client game completed\n";
            (void) client1.close ();
            (void) client2.close ();
            return true;
        }
        catch (const std::exception &ex) {
            log << "client verification failed " << ex.what () << '\n';
            (void) client1.close ();
            (void) client2.close ();
            return false;
        }
    }

#undef ensure
#undef ensure_result

  private:
    static void ensure (bool condition, const char *expression)
    {
        if (!condition) { throw std::runtime_error (std::string ("Ensure failed: ") + expression); }
    }

    template <typename T>
    static T ensure_result (stream_connector::result_t<T> result, const char *expression)
    {
        if (!result) {
            const auto message =
              result.error () ? result.error ()->message : "operation failed";
            throw std::runtime_error (std::string ("Ensure failed: ") + expression
                                      + ": " + message);
        }
        return result.value ();
    }

    static void ensure_no_self_join_notify (stream_connector::connector_t &client,
                                            const std::string &actor_id)
    {
        auto notify =
          client.wait_for<player_joined_notify_t> ()
            .where ([&actor_id] (const player_joined_notify_t &message) {
                return message.actor_id == actor_id;
            })
            .timeout (std::chrono::milliseconds (25))
            .submit ();
        ensure (!notify, "self join notify must not be delivered");
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
