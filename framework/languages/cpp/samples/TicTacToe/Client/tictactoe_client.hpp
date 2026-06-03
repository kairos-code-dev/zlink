/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "tictactoe_player_client.hpp"
#include "../Shared/E2E/client_e2e_server.hpp"

#include <zlink/Contracts/Sockets/stream_socket.hpp>
#include <zlink/Contracts/Service/operation_contracts.hpp>
#include <zlink/framework.hpp>
#include <zlink/http_client.hpp>

#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace zlink::samples::tictactoe
{

inline std::string
make_sample_api_http_endpoint ()
{
  zlink::context_t context;
  zlink::stream_socket_t probe (context);
  probe.options ().notify (false);
  probe.bind ("tcp://127.0.0.1:0");
  const auto endpoint = probe.options ().last_endpoint ();
  probe.close ();
  const auto colon = endpoint.rfind (':');
  if (colon == std::string::npos || colon + 1 >= endpoint.size ()) {
    throw std::runtime_error ("sample API endpoint port allocation failed");
  }
  return "http://127.0.0.1:" + endpoint.substr (colon + 1);
}

class tictactoe_client_t
{
public:
  tictactoe_client_run_result_t run (
    const tictactoe_client_options_t &options)
  {
    zlink::context_t context;
    zlink::stream_socket_t server (context);

    tictactoe_client_options_t run_options = options;
    zlink::framework::app_t api_app;
    int api_exit_code = -1;
    std::thread server_thread;
    std::thread api_thread;
    if (run_options.use_embedded_server) {
      reset_sample_log ();
      server.options ().notify (false);
      server.bind ("tcp://127.0.0.1:0");
      run_options.play_endpoint = server.options ().last_endpoint ();
      run_options.api_http_endpoint = make_sample_api_http_endpoint ();
      server_thread = std::thread ([&server,
                                    endpoint = run_options.play_endpoint,
                                    x_actor_id = run_options.x_actor_id] {
        run_client_e2e_stream_server (server, endpoint, x_actor_id);
      });

      sample_topology_t api_topology;
      api_topology.api_http_endpoint = run_options.api_http_endpoint;
      api_topology.stream_endpoint = run_options.play_endpoint;
      api_app = build_client_e2e_api_server (api_topology);
      api_thread = std::thread ([&api_app, &api_exit_code] {
        const char *argv_raw[] = { "tictactoe-api" };
        auto **argv = const_cast<char **> (argv_raw);
        api_exit_code = api_app.run (1, argv);
      });
    }

    auto http_client = zlink::http_client::client_t::create ()
                         .base_url (run_options.api_http_endpoint)
                         .json ()
                         .timeout (std::chrono::milliseconds (500))
                         .build ();
    bool http_ready = false;
    for (int attempt = 0; attempt < 100 && !http_ready; ++attempt) {
      auto ready = http_client.post ("/games")
                     .body (create_match_req_t { run_options.x_actor_id })
                     .submit<create_match_res_t> ()
                     .result ();
      http_ready =
        ready.has_value () && !ready.value ().body.play_endpoint.empty ();
      if (http_ready) {
        run_options.game_name = ready.value ().body.match_id;
        run_options.play_endpoint = ready.value ().body.play_endpoint;
      } else {
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
      }
    }

    auto x =
      tictactoe_player_client_t::connect (run_options.x_actor_id, run_options);
    auto o =
      tictactoe_player_client_t::connect (run_options.o_actor_id, run_options);

    tictactoe_client_run_result_t result;
    result.connected = x.connected () && o.connected ();
    result.game_name = run_options.game_name;
    result.api_endpoint = run_options.api_http_endpoint;
    result.play_endpoint = run_options.play_endpoint;
    result.http_game_created = http_ready;
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
    if (api_thread.joinable ()) {
      api_app.stop ();
      api_thread.join ();
    }
    if (run_options.use_embedded_server && api_exit_code != 0) {
      result.connected = false;
    }
    if (server_thread.joinable ()) {
      server_thread.join ();
    }
    return result;
  }
};

} // namespace zlink::samples::tictactoe
