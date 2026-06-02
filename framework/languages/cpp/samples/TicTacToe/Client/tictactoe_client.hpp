/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "tictactoe_player_client.hpp"
#include "../../Shared/stream_frame_server.hpp"

#include <zlink/Contracts/Sockets/stream_socket.hpp>
#include <zlink/Contracts/Service/operation_contracts.hpp>

#include <fstream>
#include <string>
#include <thread>

namespace zlink::samples::tictactoe
{

class tictactoe_client_t
{
public:
  tictactoe_client_run_result_t run (
    const tictactoe_client_options_t &options)
  {
    zlink::context_t context;
    zlink::stream_socket_t server (context);
    server.options ().notify (false);
    server.bind ("tcp://127.0.0.1:0");

    tictactoe_client_options_t run_options = options;
    run_options.play_endpoint = server.options ().last_endpoint ();
    std::thread server_thread ([&server,
                                endpoint = run_options.play_endpoint,
                                game_name = run_options.game_name,
                                x_actor_id = run_options.x_actor_id] {
      std::ofstream log ("tictactoe-server.log", std::ios::trunc);
      log << "bind " << endpoint << '\n';
      int handled = 0;
      std::string buffer;
      while (handled < 9) {
        zlink::received_t inbound;
        if (server.recv (inbound) != 0) {
          log << "recv failed index=" << handled << '\n';
          return;
        }
        buffer += inbound.parts ().empty () ? std::string {}
                                            : inbound.parts ()[0].to_string ();
        while (auto frame = zlink::samples::try_read_stream_frame (buffer)) {
          log << "recv " << frame->name << '\n';
          tictactoe_state_t state;
          state.match_id = game_name;
          state.status = "playing";
          zlink::samples::send_stream_reply_and_push (
            inbound,
            *frame,
            place_mark_res_t { state },
            turn_changed_notify_t::packet_name,
            turn_changed_notify_t { state.match_id, x_actor_id, state });
          log << "reply " << frame->name << '\n';
          log << "push " << turn_changed_notify_t::packet_name << '\n';
          ++handled;
        }
        inbound.close ();
      }
    });

    auto x =
      tictactoe_player_client_t::connect (run_options.x_actor_id, run_options);
    auto o =
      tictactoe_player_client_t::connect (run_options.o_actor_id, run_options);

    tictactoe_client_run_result_t result;
    result.connected = x.connected () && o.connected ();
    result.game_name = run_options.game_name;
    result.api_endpoint = run_options.api_endpoint;
    result.requests.push_back (x.authenticate ());
    result.requests.push_back (o.authenticate ());
    result.requests.push_back (x.join_game (run_options.game_name));
    result.requests.push_back (o.join_game (run_options.game_name));
    result.requests.push_back (x.place_mark (run_options.game_name, 0));
    result.requests.push_back (o.place_mark (run_options.game_name, 3));
    result.requests.push_back (x.place_mark (run_options.game_name, 1));
    result.requests.push_back (o.place_mark (run_options.game_name, 4));
    result.requests.push_back (x.place_mark (run_options.game_name, 2));

    x.dispatch ();
    o.dispatch ();
    result.opponent_joined_notifications =
      x.notifications ().opponents.size () + o.notifications ().opponents.size ();
    result.turn_changed_notifications =
      x.notifications ().turns.size () + o.notifications ().turns.size ();
    result.game_ended_notifications =
      x.notifications ().ended.size () + o.notifications ().ended.size ();

    x.close ();
    o.close ();
    server_thread.join ();
    return result;
  }
};

} // namespace zlink::samples::tictactoe
