/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "tictactoe_player_client.hpp"
#include "../Server/Api/Handlers/create_match_handler.hpp"
#include "../Shared/sample_log.hpp"
#include "../../Shared/stream_frame_server.hpp"

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
    reset_sample_log ();
    zlink::stream_socket_t server (context);
    server.options ().notify (false);
    server.bind ("tcp://127.0.0.1:0");

    tictactoe_client_options_t run_options = options;
    run_options.play_endpoint = server.options ().last_endpoint ();
    run_options.api_http_endpoint = "http://127.0.0.1:0";
    std::thread server_thread ([&server,
                                endpoint = run_options.play_endpoint,
                                x_actor_id = run_options.x_actor_id] {
      std::ofstream log (sample_log_file, std::ios::app);
      log << "bind " << endpoint << '\n';
      log << "monitor stream ready\n";
      int handled = 0;
      std::string current_match_id = "tictactoe-game";
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
          state.match_id = current_match_id;
          state.status = "playing";
          log << "actor relay " << x_actor_id << '\n';
          if (frame->name == authenticate_req_t::packet_name) {
            const auto request =
              zlink::message_t::from (frame->payload)
                .parse_json<authenticate_req_t> ();
            zlink::samples::send_stream_reply_and_push (
              inbound,
              *frame,
              authenticate_res_t { request.actor_id },
              turn_changed_notify_t::packet_name,
              turn_changed_notify_t { state.match_id, x_actor_id, state });
          } else if (frame->name == join_match_req_t::packet_name) {
            const auto request =
              zlink::message_t::from (frame->payload)
                .parse_json<join_match_req_t> ();
            current_match_id = request.match_id;
            state.match_id = current_match_id;
            state.x_actor_id = x_actor_id;
            state.o_actor_id = request.actor_id == x_actor_id
                                 ? sample_names_t::o_actor_id
                                 : request.actor_id;
            zlink::samples::send_stream_reply_and_push (
              inbound,
              *frame,
              join_match_res_t {
                current_match_id,
                request.actor_id,
                request.actor_id == x_actor_id ? "X" : "O",
                state },
              turn_changed_notify_t::packet_name,
              turn_changed_notify_t { current_match_id, x_actor_id, state });
          } else {
            const auto request =
              zlink::message_t::from (frame->payload)
                .parse_json<place_mark_req_t> ();
            current_match_id = request.match_id;
            state.match_id = current_match_id;
            state.last_move_actor_id = request.actor_id;
            state.last_move_cell = request.cell;
            zlink::samples::send_stream_reply_and_push (
              inbound,
              *frame,
              place_mark_res_t { state },
              turn_changed_notify_t::packet_name,
              turn_changed_notify_t { current_match_id, x_actor_id, state });
          }
          log << "reply " << frame->name << '\n';
          log << "push " << turn_changed_notify_t::packet_name << '\n';
          ++handled;
        }
        inbound.close ();
      }
      log << "disconnect client\n";
      log << "shutdown server\n";
    });

    zlink::framework::app_t api_app = zlink::framework::app_t::create ();
    const auto api_http_endpoint = make_sample_api_http_endpoint ();
    sample_topology_t api_topology;
    api_topology.api_http_endpoint = api_http_endpoint;
    api_topology.stream_endpoint = run_options.play_endpoint;
    api_app.add_zlink_framework (
      [api_topology](zlink::framework::zlink_framework_options_t &options) {
        options.services ().add_singleton<sample_topology_t> (
          std::make_unique<sample_topology_t> (api_topology));
        options.services ().add_singleton<create_match_room_handler_t> ();
        options.http ()
          .listen (api_topology.api_http_endpoint)
          .map_post<create_match_handler_t> ("/games");
      });
    int api_exit_code = -1;
    std::thread api_thread ([&api_app, &api_exit_code] {
      const char *argv_raw[] = { "tictactoe-api" };
      auto **argv = const_cast<char **> (argv_raw);
      api_exit_code = api_app.run (1, argv);
    });

    auto http_client = zlink::http_client::client_t::create ()
                         .base_url (api_http_endpoint)
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
    result.api_endpoint = api_http_endpoint;
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
    api_app.stop ();
    api_thread.join ();
    if (api_exit_code != 0) {
      result.connected = false;
    }
    server_thread.join ();
    return result;
  }
};

} // namespace zlink::samples::tictactoe
