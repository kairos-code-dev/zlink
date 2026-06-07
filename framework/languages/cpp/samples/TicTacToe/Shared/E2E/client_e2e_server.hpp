/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/sample.hpp"
#include "../../Shared/sample_log.hpp"
#include "../../../Shared/stream_frame_server.hpp"

#include <zlink/Contracts/Sockets/stream_socket.hpp>
#include <zlink/framework.hpp>

#include <fstream>
#include <memory>
#include <string>

namespace zlink::samples::tictactoe
{

class tictactoe_client_e2e_create_game_handler_t
{
  public:
    using request_type = create_match_req_t;
    using reply_type = create_match_res_t;
    using dependency_types = zlink::framework::dependency_list_t<
      sample_topology_t,
      zlink::framework::logger_t<tictactoe_client_e2e_create_game_handler_t>>;

    explicit tictactoe_client_e2e_create_game_handler_t (
      sample_topology_t &topology,
      zlink::framework::logger_t<tictactoe_client_e2e_create_game_handler_t> &logger) :
        _topology (topology), _logger (logger)
    {
    }

    create_match_res_t handle (const create_match_req_t &request)
    {
        _logger.info ("http POST /games");
        _logger.info (std::string ("recv ") + create_match_req_t::packet_name);
        _logger.info (std::string ("reply ") + create_match_req_t::packet_name);
        const auto owner = request.owner_actor_id.empty ()
                             ? std::string (sample_names_t::x_actor_id)
                             : request.owner_actor_id;
        return {"match-1", owner, _topology.stream_endpoint};
    }

  private:
    sample_topology_t &_topology;
    zlink::framework::logger_t<tictactoe_client_e2e_create_game_handler_t> _logger;
};

inline zlink::framework::app_t build_client_e2e_api_server (sample_topology_t topology)
{
    auto app = zlink::framework::app_t::create ();
    app.logging ().use_file (sample_log_file);
    app.add_zlink_framework ([topology] (zlink::framework::zlink_framework_options_t &options) {
        options.services ().add_singleton<sample_topology_t> (
          std::make_unique<sample_topology_t> (topology));
        options.http ()
          .listen (topology.api_http_endpoint)
          .map_post<tictactoe_client_e2e_create_game_handler_t> ("/games");
    });
    return app;
}

inline void run_client_e2e_stream_server (zlink::stream_socket_t &server,
                                          const std::string &endpoint,
                                          const std::string &x_actor_id)
{
    std::ofstream log (sample_log_file, std::ios::app);
    log << "bind " << endpoint << '\n';
    log << "monitor stream ready\n";
    log << "monitor event stream_ready\n";
    log.flush ();
    int handled = 0;
    std::string current_match_id = "tictactoe-game";
    std::string buffer;
    while (handled < 9) {
        zlink::received_t inbound;
        if (server.recv (inbound) != 0) {
            log << "recv failed index=" << handled << '\n';
            return;
        }
        buffer += inbound.parts ().empty () ? std::string{} : inbound.parts ()[0].to_string ();
        while (auto frame = zlink::samples::try_read_stream_frame (buffer)) {
            log << "recv " << frame->name << '\n';
            tictactoe_state_t state;
            state.match_id = current_match_id;
            state.status = "playing";
            log << "actor relay " << x_actor_id << '\n';
            if (frame->name == authenticate_req_t::packet_name) {
                const auto request =
                  zlink::message_t::from (frame->payload).parse_json<authenticate_req_t> ();
                zlink::samples::send_stream_reply_and_push (
                  inbound, *frame, authenticate_res_t{request.actor_id},
                  turn_changed_notify_t::packet_name,
                  turn_changed_notify_t{state.match_id, x_actor_id, state});
            } else if (frame->name == join_match_req_t::packet_name) {
                const auto request =
                  zlink::message_t::from (frame->payload).parse_json<join_match_req_t> ();
                current_match_id = request.match_id;
                state.match_id = current_match_id;
                state.x_actor_id = x_actor_id;
                state.o_actor_id =
                  request.actor_id == x_actor_id ? sample_names_t::o_actor_id : request.actor_id;
                zlink::samples::send_stream_reply_and_push (
                  inbound, *frame,
                  join_match_res_t{current_match_id, request.actor_id,
                                   request.actor_id == x_actor_id ? "X" : "O", state},
                  turn_changed_notify_t::packet_name,
                  turn_changed_notify_t{current_match_id, x_actor_id, state});
            } else {
                const auto request =
                  zlink::message_t::from (frame->payload).parse_json<place_mark_req_t> ();
                current_match_id = request.match_id;
                state.match_id = current_match_id;
                state.last_move_actor_id = request.actor_id;
                state.last_move_cell = request.cell;
                zlink::samples::send_stream_reply_and_push (
                  inbound, *frame, place_mark_res_t{state}, turn_changed_notify_t::packet_name,
                  turn_changed_notify_t{current_match_id, x_actor_id, state});
            }
            log << "reply " << frame->name << '\n';
            log << "push " << turn_changed_notify_t::packet_name << '\n';
            ++handled;
        }
        inbound.close ();
    }
    log << "disconnect client\n";
    log << "shutdown server\n";
}

} // namespace zlink::samples::tictactoe
