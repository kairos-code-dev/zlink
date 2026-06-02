/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "bingo_player_client.hpp"
#include "../Shared/Configuration/sample_names.hpp"
#include "../../Shared/stream_frame_server.hpp"

#include <zlink/Contracts/Sockets/stream_socket.hpp>
#include <zlink/Contracts/Service/operation_contracts.hpp>

#include <array>
#include <fstream>
#include <string>
#include <thread>

namespace zlink::samples::bingo
{

class bingo_client_app_t
{
public:
  bingo_client_run_result_t run (const bingo_client_options_t &options)
  {
    zlink::context_t context;
    zlink::stream_socket_t server (context);
    server.options ().notify (false);
    server.bind ("tcp://127.0.0.1:0");

    bingo_client_options_t run_options = options;
    run_options.stream_endpoint = server.options ().last_endpoint ();
    std::thread server_thread ([&server, endpoint = run_options.stream_endpoint] {
      std::ofstream log ("bingo-server.log", std::ios::trunc);
      log << "bind " << endpoint << '\n';
      int handled = 0;
      std::string buffer;
      while (handled < 10) {
        zlink::received_t inbound;
        if (server.recv (inbound) != 0) {
          log << "recv failed index=" << handled << '\n';
          return;
        }
        buffer += inbound.parts ().empty () ? std::string {}
                                            : inbound.parts ()[0].to_string ();
        while (auto frame = zlink::samples::try_read_stream_frame (buffer)) {
          log << "recv " << frame->name << '\n';
          if (frame->kind ==
              zlink::stream_connector::message_kind_t::request) {
            bingo_room_state_t state;
            state.room_id = "room-1";
            state.status = "playing";
            zlink::samples::send_stream_reply (
              inbound,
              *frame,
              match_bingo_res_t { state.room_id, state });
            log << "reply " << frame->name << '\n';
            zlink::samples::send_stream_push (
              inbound,
              player_joined_notify_t::packet_name,
              player_joined_notify_t { state.room_id,
                                       "player-1",
                                       "Player 1",
                                       1,
                                       true,
                                       state });
            log << "push " << player_joined_notify_t::packet_name << '\n';
          } else {
            bingo_room_state_t state;
            state.room_id = "room-1";
            state.status = "ended";
            zlink::samples::send_stream_push (
              inbound,
              game_ended_notify_t::packet_name,
              game_ended_notify_t { state });
            log << "push " << game_ended_notify_t::packet_name << '\n';
          }
          ++handled;
        }
        inbound.close ();
      }
    });

    std::array<bingo_player_client_t, 4> clients {
      bingo_player_client_t::connect ("player-1", run_options),
      bingo_player_client_t::connect ("player-2", run_options),
      bingo_player_client_t::connect ("player-3", run_options),
      bingo_player_client_t::connect ("player-4", run_options)
    };

    bingo_client_run_result_t result;
    result.connected = true;
    for (auto &client : clients) {
      result.connected = result.connected && client.connected ();
      result.requests.push_back (client.authenticate ());
    }

    result.requests.push_back (clients[0].match ());
    result.requests.push_back (clients[1].match ());
    result.requests.push_back (clients[2].match ());
    result.requests.push_back (clients[3].match ());
    result.requests.push_back (clients[0].start ("room-1"));
    result.sends.push_back (clients[3].leave ("room-1"));

    for (auto &client : clients) {
      client.dispatch ();
      result.player_joined_notifications +=
        client.notifications ().joined.size ();
      result.started_notifications += client.notifications ().started.size ();
      result.drawn_notifications += client.notifications ().drawn.size ();
      result.ended_notifications += client.notifications ().ended.size ();
      client.close ();
    }
    server_thread.join ();
    return result;
  }
};

} // namespace zlink::samples::bingo
