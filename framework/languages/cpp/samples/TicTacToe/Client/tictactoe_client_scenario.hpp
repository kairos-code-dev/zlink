/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "tictactoe_client_options.hpp"
#include "../Shared/Contracts/messages.hpp"

#include <zlink/stream_connector.hpp>

#include <chrono>
#include <fstream>
#include <stdexcept>
#include <string>

#define ensure(condition) ensure ((condition), #condition)
#define ensure_result(result) ensure_result ((result), #result)

namespace zlink::samples::tictactoe
{

inline constexpr const char *tictactoe_client_log_file = "tictactoe-client.log";

class tictactoe_client_scenario_t
{
  public:
    bool run (stream_connector::connector_t &client1,
              stream_connector::connector_t &client2,
              const create_game_http_res_t &room,
              const tictactoe_client_options_t &options)
    {
        std::ofstream log (tictactoe_client_log_file, std::ios::trunc);

        try {
            ensure (!room.room_id.empty ());
            ensure (room.room_id == "room-1");
            ensure (room.play_endpoint.rfind ("tcp://127.0.0.1:", 0) == 0);
            ensure (room.game_name == options.game_name);

            auto client1_connect = client1.connect ();
            auto client2_connect = client2.connect ();
            ensure (client1_connect && client2_connect);

            auto client1_auth = ensure_result (client1.request<authenticate_res_t> (authenticate_req_t{options.x_actor_id}).submit ());
            ensure (client1_auth.actor_id == options.x_actor_id);

            auto client2_auth = ensure_result (client2.request<authenticate_res_t> (authenticate_req_t{options.o_actor_id}).submit ());
            ensure (client2_auth.actor_id == options.o_actor_id);
            ensure (client2_auth.actor_id != client1_auth.actor_id);

            auto client1_join = ensure_result (client1.request<join_game_res_t> (join_game_req_t{room.room_id}).submit ());
            ensure (client1_join.state.room_id == room.room_id);
            ensure (client1_join.state.x_actor_id == options.x_actor_id);
            ensure (client1_join.state.o_actor_id.empty ());
            ensure (client1_join.state.status == "WaitingForPlayers");
            ensure (client1_join.state.next_turn == options.x_actor_id);
            ensure_no_self_join_notify (client1, options.x_actor_id);

            auto client2_join = ensure_result (client2.request<join_game_res_t> (join_game_req_t{room.room_id}).submit ());
            ensure (client2_join.state.room_id == room.room_id);
            ensure (client2_join.state.x_actor_id == options.x_actor_id);
            ensure (client2_join.state.o_actor_id == options.o_actor_id);
            ensure (client2_join.state.status == "InProgress");
            ensure_no_self_join_notify (client2, options.o_actor_id);
            auto client1_saw_client2_join = ensure_result (client1.wait_for<player_joined_notify_t> ().where ([&client2_auth] (const player_joined_notify_t &message) { return message.actor_id == client2_auth.actor_id; }).submit ());
            ensure (client1_saw_client2_join.room_id == room.room_id);
            ensure (client1_saw_client2_join.mark == "O");

            auto client1_first_move = ensure_result (client1.request<place_mark_res_t> (place_mark_req_t{0}).submit ());
            ensure (client1_first_move.state.room_id == room.room_id);
            ensure (client1_first_move.state.last_move_actor_id == options.x_actor_id);
            ensure (client1_first_move.state.last_move_cell == 0);
            auto client2_saw_first_move = ensure_result (client2.wait_for<game_state_notify_t> ().where ([&options] (const game_state_notify_t &message) { return message.state.last_move_actor_id == options.x_actor_id && message.state.last_move_cell == 0; }).submit ());
            ensure (client2_saw_first_move.room_id == room.room_id);
            ensure (client2_saw_first_move.state.last_move_cell == 0);

            auto client2_first_move = ensure_result (client2.request<place_mark_res_t> (place_mark_req_t{3}).submit ());
            ensure (client2_first_move.state.room_id == room.room_id);
            ensure (client2_first_move.state.last_move_actor_id == options.o_actor_id);
            ensure (client2_first_move.state.last_move_cell == 3);
            auto client1_saw_first_o_move = ensure_result (client1.wait_for<game_state_notify_t> ().where ([&options] (const game_state_notify_t &message) { return message.state.last_move_actor_id == options.o_actor_id && message.state.last_move_cell == 3; }).submit ());
            ensure (client1_saw_first_o_move.room_id == room.room_id);
            ensure (client1_saw_first_o_move.state.last_move_cell == 3);

            auto client1_second_move = ensure_result (client1.request<place_mark_res_t> (place_mark_req_t{1}).submit ());
            ensure (client1_second_move.state.room_id == room.room_id);
            ensure (client1_second_move.state.last_move_actor_id == options.x_actor_id);
            ensure (client1_second_move.state.last_move_cell == 1);
            auto client2_saw_second_x_move = ensure_result (client2.wait_for<game_state_notify_t> ().where ([&options] (const game_state_notify_t &message) { return message.state.last_move_actor_id == options.x_actor_id && message.state.last_move_cell == 1; }).submit ());
            ensure (client2_saw_second_x_move.room_id == room.room_id);
            ensure (client2_saw_second_x_move.state.last_move_cell == 1);

            auto client2_second_move = ensure_result (client2.request<place_mark_res_t> (place_mark_req_t{4}).submit ());
            ensure (client2_second_move.state.room_id == room.room_id);
            ensure (client2_second_move.state.last_move_actor_id == options.o_actor_id);
            ensure (client2_second_move.state.last_move_cell == 4);
            auto client1_saw_second_o_move = ensure_result (client1.wait_for<game_state_notify_t> ().where ([&options] (const game_state_notify_t &message) { return message.state.last_move_actor_id == options.o_actor_id && message.state.last_move_cell == 4; }).submit ());
            ensure (client1_saw_second_o_move.room_id == room.room_id);
            ensure (client1_saw_second_o_move.state.last_move_cell == 4);

            auto client1_winning_move = ensure_result (client1.request<place_mark_res_t> (place_mark_req_t{2}).submit ());
            ensure (client1_winning_move.state.room_id == room.room_id);
            ensure (client1_winning_move.state.last_move_actor_id == options.x_actor_id);
            ensure (client1_winning_move.state.last_move_cell == 2);
            ensure (client1_winning_move.state.board == "XXXOO....");
            ensure (client1_winning_move.state.status == "Won");
            ensure (client1_winning_move.state.winner == options.x_actor_id);
            auto client2_saw_winning_move = ensure_result (client2.wait_for<game_state_notify_t> ().where ([&options] (const game_state_notify_t &message) { return message.state.winner == options.x_actor_id; }).submit ());
            ensure (client2_saw_winning_move.room_id == room.room_id);
            ensure (client2_saw_winning_move.state.board == "XXXOO....");
            ensure (client2_saw_winning_move.state.status == "Won");

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

inline void register_tictactoe_client_codecs (stream_connector::connector_t &connector)
{
    connector.codecs ()
      .add_message_pack<authenticate_req_t> ()
      .add_message_pack<authenticate_res_t> ()
      .add_message_pack<join_game_req_t> ()
      .add_message_pack<join_game_res_t> ()
      .add_message_pack<place_mark_req_t> ()
      .add_message_pack<place_mark_res_t> ()
      .add_message_pack<game_state_notify_t> ()
      .add_message_pack<game_ended_notify_t> ()
      .add_message_pack<player_joined_notify_t> ();
}

} // namespace zlink::samples::tictactoe
