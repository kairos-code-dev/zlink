/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "tictactoe_client_options.hpp"
#include "../Shared/Contracts/messages.hpp"

#include <zlink/http_client.hpp>
#include <zlink/stream_connector.hpp>
#include <zlink/stream_e2e_client.hpp>
#include <zlink/stream_e2e_client/codecs/auto_codec.hpp>

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
        (void) client1;
        (void) client2;
        (void) room;
        (void) options;
        std::cerr << "tictactoe scenario requires a third observer client\n";
        return false;
    }

    bool run (const tictactoe_client_options_t &options)
    {
        try {
            auto room = zlink::http_client::client_t::create (options.api_http_endpoint)
                          .post ("/games")
                          .body (create_game_http_req_t{options.game_name})
                          .fetch<create_game_http_res_t> ();
            if (room.owner_play_endpoint.empty ()) {
                throw std::runtime_error ("API returned an empty play endpoint.");
            }
            std::string observer_endpoint;
            for (const auto &endpoint : room.play_endpoints) {
                if (endpoint != room.owner_play_endpoint) {
                    observer_endpoint = endpoint;
                    break;
                }
            }
            if (observer_endpoint.empty ()) {
                throw std::runtime_error ("API did not return a non-owner Play endpoint.");
            }

            zlink::stream_connector::connector_options_t connector_options;
            connector_options.endpoint = room.owner_play_endpoint;
            connector_options.connect_timeout = options.stream_timeout;
            connector_options.request_timeout = options.stream_timeout;
            connector_options.dispatch_mode =
              zlink::stream_connector::dispatch_mode_t::immediate;
            auto observer_connector_options = connector_options;
            observer_connector_options.endpoint = observer_endpoint;

            auto core_client1 = zlink::stream_connector::connector_factory_t::create (
              connector_options);
            auto core_client2 = zlink::stream_connector::connector_factory_t::create (
              connector_options);
            auto core_observer = zlink::stream_connector::connector_factory_t::create (
              observer_connector_options);
            core_client1.codecs ().add_json ();
            core_client2.codecs ().add_json ();
            core_observer.codecs ().add_json ();
            [[maybe_unused]] auto inbound_log1 = core_client1.observe_inbound (
              [] (const zlink::stream_connector::inbound_observation_t &observation) {
                  std::cout << "stream-inbound sample=TicTacToe client=player kind="
                            << static_cast<int> (observation.kind) << " name=" << observation.name
                            << " seq="
                            << (observation.request_seq
                                  ? std::to_string (*observation.request_seq)
                                  : std::string ("-"))
                            << " bytes=" << observation.payload_length << '\n';
              });
            [[maybe_unused]] auto inbound_log2 = core_client2.observe_inbound (
              [] (const zlink::stream_connector::inbound_observation_t &observation) {
                  std::cout << "stream-inbound sample=TicTacToe client=player kind="
                            << static_cast<int> (observation.kind) << " name=" << observation.name
                            << " seq="
                            << (observation.request_seq
                                  ? std::to_string (*observation.request_seq)
                                  : std::string ("-"))
                            << " bytes=" << observation.payload_length << '\n';
              });

            auto client1 = zlink::stream_e2e_client::use (core_client1);
            auto client2 = zlink::stream_e2e_client::use (core_client2);
            auto observer = zlink::stream_e2e_client::use (core_observer);
            if (!run_game (client1, client2, observer, room, options)) {
                return false;
            }

            auto recreate_client = zlink::stream_connector::connector_factory_t::create (
              connector_options);
            recreate_client.codecs ().add_json ();
            [[maybe_unused]] auto recreate_inbound_log = recreate_client.observe_inbound (
              [] (const zlink::stream_connector::inbound_observation_t &observation) {
                  std::cout << "stream-inbound sample=TicTacToe client=player kind="
                            << static_cast<int> (observation.kind) << " name=" << observation.name
                            << " seq="
                            << (observation.request_seq
                                  ? std::to_string (*observation.request_seq)
                                  : std::string ("-"))
                            << " bytes=" << observation.payload_length << '\n';
              });
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
                          stream_e2e_client::coroutine_connector_t &observer,
                          const create_game_http_res_t &room,
                          const tictactoe_client_options_t &options)
    {
        auto result = run_game_async (client1, client2, observer, room, options).result ();
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
                    stream_e2e_client::coroutine_connector_t &observer,
                    const create_game_http_res_t &room,
                    const tictactoe_client_options_t &options)
    {
        try {
            ensure (!room.room_id.empty ());
            ensure (room.room_id == "room-1");
            ensure (room.owner_play_endpoint.rfind ("tcp://127.0.0.1:", 0) == 0);
            ensure (room.required_level == 3);
            ensure (room.game_name == options.game_name);

            trace ("connect client1");
            co_await client1.connect ().async ();
            trace ("connect client2");
            co_await client2.connect ().async ();
            trace ("connect observer");
            co_await observer.connect ().async ();
            std::cout << "observer-connected endpoint=" << non_owner_endpoint (room) << '\n';

            trace ("authenticate client1");
            auto client1_auth =
              co_await stream_e2e_client::codecs::request (
                client1, authenticate_req_t{options.x_actor_id})
                .async<authenticate_res_t> ();
            ensure (client1_auth.player.actor_id == options.x_actor_id);
            ensure (client1_auth.player.wins == 99);

            trace ("authenticate client2");
            auto client2_auth =
              co_await stream_e2e_client::codecs::request (
                client2, authenticate_req_t{options.o_actor_id})
                .async<authenticate_res_t> ();
            ensure (client2_auth.player.actor_id == options.o_actor_id);
            ensure (client2_auth.player.actor_id != client1_auth.player.actor_id);

            trace ("authenticate observer");
            auto observer_auth =
              co_await stream_e2e_client::codecs::request (
                observer, authenticate_req_t{options.observer_actor_id})
                .async<authenticate_res_t> ();
            ensure (observer_auth.player.actor_id == options.observer_actor_id);
            auto observe =
              co_await stream_e2e_client::codecs::request (observer, observe_milestone_req_t{})
                .async<observe_milestone_res_t> ();
            ensure (observe.subscribed);
            std::cout << "observer-subscription=verified subscribed=true\n";

            trace ("join client1");
            auto client1_join =
              co_await stream_e2e_client::codecs::request (
                client1, join_game_req_t{room.room_id, client1_auth.player})
                .async<join_game_res_t> ();
            ensure (client1_join.state.room_id == room.room_id);
            ensure (client1_join.state.x_actor_id == options.x_actor_id);
            ensure (client1_join.state.o_actor_id.empty ());
            ensure (client1_join.state.status == tictactoe_status_t::waiting_for_players);
            ensure (client1_join.state.next_turn == options.x_actor_id);
            auto client1_self_join =
              stream_e2e_client::codecs::wait_for<player_joined_notify_t> (client1)
                .where (&player_joined_notify_t::actor_id, options.x_actor_id)
                .timeout (std::chrono::milliseconds (25))
                .async ()
                .result ();
            require_condition (!client1_self_join, "self join notify must not be delivered");

            auto client1_wait_client2_join =
              stream_e2e_client::codecs::wait_for<player_joined_notify_t> (client1)
                .where (&player_joined_notify_t::actor_id, client2_auth.player.actor_id)
                .async ();
            client1_wait_client2_join.start ();
            trace ("join client2");
            auto client2_join =
              co_await stream_e2e_client::codecs::request (
                client2, join_game_req_t{room.room_id, client2_auth.player})
                .async<join_game_res_t> ();
            ensure (client2_join.state.room_id == room.room_id);
            ensure (client2_join.state.x_actor_id == options.x_actor_id);
            ensure (client2_join.state.o_actor_id == options.o_actor_id);
            ensure (client2_join.state.status == tictactoe_status_t::in_progress);
            auto client2_self_join =
              stream_e2e_client::codecs::wait_for<player_joined_notify_t> (client2)
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
              stream_e2e_client::codecs::wait_for<game_state_notify_t> (client2)
                .where ([&options] (const game_state_notify_t &message) {
                    return message.state.last_move_actor_id == options.x_actor_id
                           && message.state.last_move_cell == 0;
                })
                .async ();
            client2_wait_first_move.start ();
            trace ("client1 first move");
            auto client1_first_move =
              co_await stream_e2e_client::codecs::request (client1, place_mark_req_t{0})
                .async<place_mark_res_t> ();
            ensure (client1_first_move.state.room_id == room.room_id);
            ensure (client1_first_move.state.last_move_actor_id == options.x_actor_id);
            ensure (client1_first_move.state.last_move_cell == 0);
            trace ("client2 saw first move");
            auto client2_saw_first_move = co_await client2_wait_first_move;
            ensure (client2_saw_first_move.room_id == room.room_id);
            ensure (client2_saw_first_move.state.last_move_cell == 0);

            auto client1_wait_first_o_move =
              stream_e2e_client::codecs::wait_for<game_state_notify_t> (client1)
                .where ([&options] (const game_state_notify_t &message) {
                    return message.state.last_move_actor_id == options.o_actor_id
                           && message.state.last_move_cell == 3;
                })
                .async ();
            client1_wait_first_o_move.start ();
            trace ("client2 first move");
            auto client2_first_move =
              co_await stream_e2e_client::codecs::request (client2, place_mark_req_t{3})
                .async<place_mark_res_t> ();
            ensure (client2_first_move.state.room_id == room.room_id);
            ensure (client2_first_move.state.last_move_actor_id == options.o_actor_id);
            ensure (client2_first_move.state.last_move_cell == 3);
            trace ("client1 saw first o move");
            auto client1_saw_first_o_move = co_await client1_wait_first_o_move;
            ensure (client1_saw_first_o_move.room_id == room.room_id);
            ensure (client1_saw_first_o_move.state.last_move_cell == 3);

            auto client2_wait_second_x_move =
              stream_e2e_client::codecs::wait_for<game_state_notify_t> (client2)
                .where ([&options] (const game_state_notify_t &message) {
                    return message.state.last_move_actor_id == options.x_actor_id
                           && message.state.last_move_cell == 1;
                })
                .async ();
            client2_wait_second_x_move.start ();
            trace ("client1 second move");
            auto client1_second_move =
              co_await stream_e2e_client::codecs::request (client1, place_mark_req_t{1})
                .async<place_mark_res_t> ();
            ensure (client1_second_move.state.room_id == room.room_id);
            ensure (client1_second_move.state.last_move_actor_id == options.x_actor_id);
            ensure (client1_second_move.state.last_move_cell == 1);
            trace ("client2 saw second x move");
            auto client2_saw_second_x_move = co_await client2_wait_second_x_move;
            ensure (client2_saw_second_x_move.room_id == room.room_id);
            ensure (client2_saw_second_x_move.state.last_move_cell == 1);

            auto client1_wait_second_o_move =
              stream_e2e_client::codecs::wait_for<game_state_notify_t> (client1)
                .where ([&options] (const game_state_notify_t &message) {
                    return message.state.last_move_actor_id == options.o_actor_id
                           && message.state.last_move_cell == 4;
                })
                .async ();
            client1_wait_second_o_move.start ();
            trace ("client2 second move");
            auto client2_second_move =
              co_await stream_e2e_client::codecs::request (client2, place_mark_req_t{4})
                .async<place_mark_res_t> ();
            ensure (client2_second_move.state.room_id == room.room_id);
            ensure (client2_second_move.state.last_move_actor_id == options.o_actor_id);
            ensure (client2_second_move.state.last_move_cell == 4);
            trace ("client1 saw second o move");
            auto client1_saw_second_o_move = co_await client1_wait_second_o_move;
            ensure (client1_saw_second_o_move.room_id == room.room_id);
            ensure (client1_saw_second_o_move.state.last_move_cell == 4);

            auto client2_wait_winning_move =
              stream_e2e_client::codecs::wait_for<game_state_notify_t> (client2)
                .where ([&options] (const game_state_notify_t &message) {
                    return message.state.winner == options.x_actor_id;
                })
                .async ();
            client2_wait_winning_move.start ();
            auto observer_wait_milestone =
              stream_e2e_client::codecs::wait_for<win_milestone_notify_t> (observer)
                .where ([&options] (const win_milestone_notify_t &message) {
                    return message.actor_id == options.x_actor_id && message.wins == 100;
                })
                .async ();
            observer_wait_milestone.start ();
            trace ("client1 winning move");
            auto client1_winning_move =
              co_await stream_e2e_client::codecs::request (client1, place_mark_req_t{2})
                .async<place_mark_res_t> ();
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
            auto milestone = co_await observer_wait_milestone;
            ensure (milestone.room_id == room.room_id);
            ensure (milestone.actor_id == options.x_actor_id);
            ensure (milestone.wins == 100);
            ensure (milestone.receiving_spot_node_rid == non_owner_node_rid (room));
            std::cout << "observer-win-milestone=verified actor=" << milestone.actor_id
                      << " wins=" << milestone.wins
                      << " receivingSpotNodeRid=" << milestone.receiving_spot_node_rid << '\n';

            co_await stream_e2e_client::codecs::request (client1,
                                                         leave_game_req_t{room.room_id})
              .async<place_mark_res_t> ();
            co_await stream_e2e_client::codecs::request (client2,
                                                         leave_game_req_t{room.room_id})
              .async<place_mark_res_t> ();

            co_await client1.close ().async ();
            co_await client2.close ().async ();
            co_await observer.close ().async ();
            std::cout << "tictactoe completed\n";
            co_return true;
        }
        catch (const std::exception &ex) {
            std::cerr << "tictactoe game failed: " << ex.what () << '\n';
            (void) client1.close ();
            (void) client2.close ();
            (void) observer.close ();
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
              co_await stream_e2e_client::codecs::request (
                client, authenticate_req_t{options.x_actor_id})
                .async<authenticate_res_t> ();
            ensure (recreated.player.actor_id == options.x_actor_id);
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

    static std::string non_owner_endpoint (const create_game_http_res_t &room)
    {
        for (const auto &endpoint : room.play_endpoints) {
            if (endpoint != room.owner_play_endpoint) {
                return endpoint;
            }
        }
        return {};
    }

    static std::string non_owner_node_rid (const create_game_http_res_t &room)
    {
        const auto endpoint = non_owner_endpoint (room);
        for (const auto &node : room.play_nodes) {
            if (node.stream_endpoint == endpoint) {
                return node.spot_node_rid;
            }
        }
        return {};
    }

};

#undef ensure

} // namespace zlink::samples::tictactoe
