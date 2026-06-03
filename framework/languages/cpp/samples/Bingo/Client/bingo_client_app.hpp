/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "bingo_player_client.hpp"
#include "../Shared/E2E/client_e2e_stream_server.hpp"

#include <zlink/Contracts/Sockets/stream_socket.hpp>
#include <zlink/Contracts/Service/operation_contracts.hpp>

#include <array>
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

    bingo_client_options_t run_options = options;
    std::thread server_thread;
    if (run_options.use_embedded_server) {
      server.options ().notify (false);
      server.bind ("tcp://127.0.0.1:0");
      run_options.stream_endpoint = server.options ().last_endpoint ();
      server_thread = std::thread (
        [&server, endpoint = run_options.stream_endpoint] {
          run_client_e2e_stream_server (server, endpoint);
        });
    }

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
    if (server_thread.joinable ()) {
      server_thread.join ();
    }
    return result;
  }
};

} // namespace zlink::samples::bingo
