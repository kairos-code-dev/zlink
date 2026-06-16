/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "tictactoe_client_options.hpp"
#include "../Shared/Contracts/messages.hpp"

#include <zlink/http_client.hpp>
#include <zlink/stream_connector.hpp>
#include <zlink/stream_e2e_client.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

#define ensure(condition) require_condition ((condition), #condition)

namespace zlink::samples::tictactoe
{

class tictactoe_client_scenario_t
{
  public:
    bool run (stream_e2e_client::coroutine_connector_t &client1,
              stream_e2e_client::coroutine_connector_t &client2,
              const create_game_http_res_t &room,
              const tictactoe_client_options_t &options)
    {
        return run_game (client1, client2, room, options);
    }

    bool run (const tictactoe_client_options_t &options)
    {
        try {
            auto room = zlink::http_client::client_t::create (options.api_http_endpoint)
                          .post ("/games")
                          .body (create_game_http_req_t{options.game_name})
                          .fetch<create_game_http_res_t> ();
            if (room.play_endpoint.empty ()) {
                throw std::runtime_error ("API returned an empty play endpoint.");
            }

            zlink::stream_connector::connector_options_t connector_options;
            connector_options.endpoint = room.play_endpoint;
            connector_options.connect_timeout = options.stream_timeout;
            connector_options.request_timeout = options.stream_timeout;
            connector_options.dispatch_mode =
              zlink::stream_connector::dispatch_mode_t::immediate;

            auto core_client1 = zlink::stream_connector::connector_factory_t::create (
              connector_options);
            auto core_client2 = zlink::stream_connector::connector_factory_t::create (
              connector_options);
            core_client1.codecs ().add_json ();
            core_client2.codecs ().add_json ();

            auto client1 = zlink::stream_e2e_client::use (core_client1);
            auto client2 = zlink::stream_e2e_client::use (core_client2);
            if (!run_game (client1, client2, room, options)) {
                return false;
            }

            auto recreate_client = zlink::stream_connector::connector_factory_t::create (
              connector_options);
            recreate_client.codecs ().add_json ();
            auto client3 = zlink::stream_e2e_client::use (recreate_client);
            return run_recreate_check (client3, options);
        }
        catch (const std::exception &ex) {
            std::cerr << "tictactoe scenario failed: " << ex.what () << '\n';
            return false;
        }
    }

  private:
    static bool run_game (stream_e2e_client::coroutine_connector_t &client1,
                          stream_e2e_client::coroutine_connector_t &client2,
                          const create_game_http_res_t &room,
                          const tictactoe_client_options_t &options)
    {
        auto result = run_game_async (client1, client2, room, options).result ();
        return result && result.value ();
    }

    static bool run_recreate_check (stream_e2e_client::coroutine_connector_t &client,
                                    const tictactoe_client_options_t &options)
    {
        auto result = run_recreate_check_async (client, options).result ();
        return result && result.value ();
    }

    static stream_e2e_client::task_t<bool>
    run_game_async (stream_e2e_client::coroutine_connector_t &client1,
                    stream_e2e_client::coroutine_connector_t &client2,
                    const create_game_http_res_t &room,
                    const tictactoe_client_options_t &options)
    {
        try {
            ensure (!room.room_id.empty ());
            ensure (room.room_id == "room-1");
            ensure (room.play_endpoint.rfind ("tcp://127.0.0.1:", 0) == 0);
            ensure (room.game_name == options.game_name);

            trace ("connect client1");
            co_await client1.connect ().async ();
            trace ("connect client2");
            co_await client2.connect ().async ();

            trace ("authenticate client1");
            auto client1_auth =
              co_await client1.request (authenticate_req_t{options.x_actor_id})
                .async<authenticate_res_t> ();
            ensure (client1_auth.actor_id == options.x_actor_id);

            trace ("authenticate client2");
            auto client2_auth =
              co_await client2.request (authenticate_req_t{options.o_actor_id})
                .async<authenticate_res_t> ();
            ensure (client2_auth.actor_id == options.o_actor_id);
            ensure (client2_auth.actor_id != client1_auth.actor_id);

            trace ("join client1");
            auto client1_join =
              co_await client1.request (join_game_req_t{room.room_id})
                .async<join_game_res_t> ();
            ensure (client1_join.state.room_id == room.room_id);
            ensure (client1_join.state.x_actor_id == options.x_actor_id);
            ensure (client1_join.state.o_actor_id.empty ());
            ensure (client1_join.state.status == tictactoe_status_t::waiting_for_players);
            ensure (client1_join.state.next_turn == options.x_actor_id);
            auto client1_self_join =
              client1.wait_for<player_joined_notify_t> ()
                .where (&player_joined_notify_t::actor_id, options.x_actor_id)
                .timeout (std::chrono::milliseconds (25))
                .async ()
                .result ();
            require_condition (!client1_self_join, "self join notify must not be delivered");

            auto client1_wait_client2_join =
              client1.wait_for<player_joined_notify_t> ()
                .where (&player_joined_notify_t::actor_id, client2_auth.actor_id)
                .async ();
            client1_wait_client2_join.start ();
            trace ("join client2");
            auto client2_join =
              co_await client2.request (join_game_req_t{room.room_id})
                .async<join_game_res_t> ();
            ensure (client2_join.state.room_id == room.room_id);
            ensure (client2_join.state.x_actor_id == options.x_actor_id);
            ensure (client2_join.state.o_actor_id == options.o_actor_id);
            ensure (client2_join.state.status == tictactoe_status_t::in_progress);
            auto client2_self_join =
              client2.wait_for<player_joined_notify_t> ()
                .where (&player_joined_notify_t::actor_id, options.o_actor_id)
                .timeout (std::chrono::milliseconds (25))
                .async ()
                .result ();
            require_condition (!client2_self_join, "self join notify must not be delivered");
            trace ("wait client1 saw client2 join");
            auto client1_saw_client2_join = co_await client1_wait_client2_join;
            ensure (client1_saw_client2_join.room_id == room.room_id);
            ensure (client1_saw_client2_join.mark == tictactoe_marks_t::o);

            auto client2_wait_first_move =
              client2.wait_for<game_state_notify_t> ()
                .where ([&options] (const game_state_notify_t &message) {
                    return message.state.last_move_actor_id == options.x_actor_id
                           && message.state.last_move_cell == 0;
                })
                .async ();
            client2_wait_first_move.start ();
            trace ("client1 first move");
            auto client1_first_move =
              co_await client1.request (place_mark_req_t{0}).async<place_mark_res_t> ();
            ensure (client1_first_move.state.room_id == room.room_id);
            ensure (client1_first_move.state.last_move_actor_id == options.x_actor_id);
            ensure (client1_first_move.state.last_move_cell == 0);
            trace ("client2 saw first move");
            auto client2_saw_first_move = co_await client2_wait_first_move;
            ensure (client2_saw_first_move.room_id == room.room_id);
            ensure (client2_saw_first_move.state.last_move_cell == 0);

            auto client1_wait_first_o_move =
              client1.wait_for<game_state_notify_t> ()
                .where ([&options] (const game_state_notify_t &message) {
                    return message.state.last_move_actor_id == options.o_actor_id
                           && message.state.last_move_cell == 3;
                })
                .async ();
            client1_wait_first_o_move.start ();
            trace ("client2 first move");
            auto client2_first_move =
              co_await client2.request (place_mark_req_t{3}).async<place_mark_res_t> ();
            ensure (client2_first_move.state.room_id == room.room_id);
            ensure (client2_first_move.state.last_move_actor_id == options.o_actor_id);
            ensure (client2_first_move.state.last_move_cell == 3);
            trace ("client1 saw first o move");
            auto client1_saw_first_o_move = co_await client1_wait_first_o_move;
            ensure (client1_saw_first_o_move.room_id == room.room_id);
            ensure (client1_saw_first_o_move.state.last_move_cell == 3);

            auto client2_wait_second_x_move =
              client2.wait_for<game_state_notify_t> ()
                .where ([&options] (const game_state_notify_t &message) {
                    return message.state.last_move_actor_id == options.x_actor_id
                           && message.state.last_move_cell == 1;
                })
                .async ();
            client2_wait_second_x_move.start ();
            trace ("client1 second move");
            auto client1_second_move =
              co_await client1.request (place_mark_req_t{1}).async<place_mark_res_t> ();
            ensure (client1_second_move.state.room_id == room.room_id);
            ensure (client1_second_move.state.last_move_actor_id == options.x_actor_id);
            ensure (client1_second_move.state.last_move_cell == 1);
            trace ("client2 saw second x move");
            auto client2_saw_second_x_move = co_await client2_wait_second_x_move;
            ensure (client2_saw_second_x_move.room_id == room.room_id);
            ensure (client2_saw_second_x_move.state.last_move_cell == 1);

            auto client1_wait_second_o_move =
              client1.wait_for<game_state_notify_t> ()
                .where ([&options] (const game_state_notify_t &message) {
                    return message.state.last_move_actor_id == options.o_actor_id
                           && message.state.last_move_cell == 4;
                })
                .async ();
            client1_wait_second_o_move.start ();
            trace ("client2 second move");
            auto client2_second_move =
              co_await client2.request (place_mark_req_t{4}).async<place_mark_res_t> ();
            ensure (client2_second_move.state.room_id == room.room_id);
            ensure (client2_second_move.state.last_move_actor_id == options.o_actor_id);
            ensure (client2_second_move.state.last_move_cell == 4);
            trace ("client1 saw second o move");
            auto client1_saw_second_o_move = co_await client1_wait_second_o_move;
            ensure (client1_saw_second_o_move.room_id == room.room_id);
            ensure (client1_saw_second_o_move.state.last_move_cell == 4);

            auto client2_wait_winning_move =
              client2.wait_for<game_state_notify_t> ()
                .where ([&options] (const game_state_notify_t &message) {
                    return message.state.winner == options.x_actor_id;
                })
                .async ();
            client2_wait_winning_move.start ();
            trace ("client1 winning move");
            auto client1_winning_move =
              co_await client1.request (place_mark_req_t{2}).async<place_mark_res_t> ();
            ensure (client1_winning_move.state.room_id == room.room_id);
            ensure (client1_winning_move.state.last_move_actor_id == options.x_actor_id);
            ensure (client1_winning_move.state.last_move_cell == 2);
            ensure (client1_winning_move.state.board == "XXXOO....");
            ensure (client1_winning_move.state.status == tictactoe_status_t::won);
            ensure (client1_winning_move.state.winner == options.x_actor_id);
            trace ("client2 saw winning move");
            auto client2_saw_winning_move = co_await client2_wait_winning_move;
            ensure (client2_saw_winning_move.room_id == room.room_id);
            ensure (client2_saw_winning_move.state.board == "XXXOO....");
            ensure (client2_saw_winning_move.state.status == tictactoe_status_t::won);

            co_await client1.close ().async ();
            co_await client2.close ().async ();
            co_return true;
        }
        catch (const std::exception &ex) {
            std::cerr << "tictactoe game failed: " << ex.what () << '\n';
            (void) client1.close ();
            (void) client2.close ();
            co_return false;
        }
    }

    static stream_e2e_client::task_t<bool>
    run_recreate_check_async (stream_e2e_client::coroutine_connector_t &client,
                              const tictactoe_client_options_t &options)
    {
        try {
            trace ("recreate connect");
            co_await client.connect ().async ();
            trace ("recreate authenticate");
            auto recreated =
              co_await client.request (authenticate_req_t{options.x_actor_id})
                .async<authenticate_res_t> ();
            ensure (recreated.actor_id == options.x_actor_id);
            co_await client.close ().async ();
            co_return true;
        }
        catch (const std::exception &ex) {
            std::cerr << "tictactoe recreate failed: " << ex.what () << '\n';
            (void) client.close ();
            co_return false;
        }
    }

    static void require_condition (bool condition, const char *expression)
    {
        if (!condition) { throw std::runtime_error (std::string ("Ensure failed: ") + expression); }
    }

    static void trace (const char *step)
    {
        std::cerr << "tictactoe step: " << step << '\n';
    }

};

#undef ensure

} // namespace zlink::samples::tictactoe
