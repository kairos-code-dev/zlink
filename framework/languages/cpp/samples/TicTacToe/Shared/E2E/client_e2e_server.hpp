/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/sample.hpp"
#include "../../Shared/sample_log.hpp"
#include "../../../Shared/stream_frame_server.hpp"

#include <zlink/Contracts/Sockets/stream_socket.hpp>
#include <zlink/framework.hpp>

#include <fstream>
#include <memory>
#include <optional>
#include <string>

namespace zlink::samples::tictactoe
{

class tictactoe_client_e2e_create_game_handler_t
{
  public:
    using request_type = create_game_http_req_t;
    using reply_type = create_game_http_res_t;
    using dependency_types = zlink::framework::dependency_list_t<
      sample_topology_t,
      zlink::framework::logger_t<tictactoe_client_e2e_create_game_handler_t>>;

    explicit tictactoe_client_e2e_create_game_handler_t (
      sample_topology_t &topology,
      zlink::framework::logger_t<tictactoe_client_e2e_create_game_handler_t> &logger) :
        _topology (topology), _logger (logger)
    {
    }

    create_game_http_res_t handle (const create_game_http_req_t &request)
    {
        const auto game_name =
          request.game_name.empty () ? std::string ("tictactoe-game") : request.game_name;
        _logger.info ("http POST /games");
        _logger.info (std::string ("recv ") + create_game_http_req_t::packet_name);
        _logger.info (std::string ("reply ") + create_game_http_res_t::packet_name);
        return {"room-1", _topology.stream_endpoint, game_name};
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
    int joins = 0;
    int moves = 0;
    std::optional<zlink::received_t> player_x_stream;
    std::optional<zlink::received_t> player_o_stream;
    zlink::stream_connector::codec_t player_x_codec = zlink::stream_connector::codec_t::message_pack;
    zlink::stream_connector::codec_t player_o_codec = zlink::stream_connector::codec_t::message_pack;
    std::string current_room_id = "tictactoe-game";
    std::string board = ".........";
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
            state.room_id = current_room_id;
            state.status = "InProgress";
            log << "actor relay " << x_actor_id << '\n';
            if (frame->name == authenticate_req_t::packet_name) {
                const auto request = zlink::samples::parse_stream_payload<authenticate_req_t> (
                  frame->payload);
                if (request.access_token == x_actor_id) {
                    player_x_stream = inbound;
                    player_x_codec = frame->codec;
                } else if (request.access_token == sample_names_t::o_actor_id) {
                    player_o_stream = inbound;
                    player_o_codec = frame->codec;
                }
                zlink::samples::send_stream_reply_and_push (
                  inbound, *frame, authenticate_res_t{request.access_token},
                  game_state_notify_t::packet_name,
                  game_state_notify_t{state.room_id, x_actor_id, state});
            } else if (frame->name == join_game_req_t::packet_name) {
                const auto request =
                  zlink::samples::parse_stream_payload<join_game_req_t> (frame->payload);
                const auto actor_id =
                  joins++ == 0 ? x_actor_id : std::string (sample_names_t::o_actor_id);
                current_room_id = request.room_id;
                state.room_id = current_room_id;
                state.x_actor_id = x_actor_id;
                state.next_turn = x_actor_id;
                if (actor_id != x_actor_id) {
                    state.o_actor_id = actor_id;
                }
                state.status = actor_id == x_actor_id ? "WaitingForPlayers" : "InProgress";
                zlink::samples::send_stream_reply_and_push (
                  inbound, *frame, join_game_res_t{state},
                  game_state_notify_t::packet_name,
                  game_state_notify_t{current_room_id, state.next_turn, state});
                if (actor_id == sample_names_t::o_actor_id && player_x_stream) {
                    zlink::samples::send_stream_push (
                      *player_x_stream, player_x_codec, player_joined_notify_t::packet_name,
                      player_joined_notify_t{current_room_id, actor_id, "O", state});
                    zlink::samples::send_stream_push (
                      *player_x_stream, player_x_codec, game_state_notify_t::packet_name,
                      game_state_notify_t{current_room_id, state.next_turn, state});
                }
            } else {
                const auto request =
                  zlink::samples::parse_stream_payload<place_mark_req_t> (frame->payload);
                const auto actor_id =
                  moves++ % 2 == 0 ? x_actor_id : std::string (sample_names_t::o_actor_id);
                board[static_cast<std::size_t> (request.cell)] =
                  actor_id == x_actor_id ? 'X' : 'O';
                state.room_id = current_room_id;
                state.board = board;
                state.last_move_actor_id = actor_id;
                state.last_move_cell = request.cell;
                if (actor_id == x_actor_id && request.cell == 2) {
                    state.status = "Won";
                    state.winner = x_actor_id;
                }
                zlink::samples::send_stream_reply_and_push (
                  inbound, *frame, place_mark_res_t{state}, game_state_notify_t::packet_name,
                  game_state_notify_t{current_room_id, x_actor_id, state});
                auto *opponent_stream =
                  actor_id == x_actor_id
                    ? (player_o_stream ? &*player_o_stream : nullptr)
                    : (player_x_stream ? &*player_x_stream : nullptr);
                const auto opponent_codec =
                  actor_id == x_actor_id ? player_o_codec : player_x_codec;
                if (opponent_stream != nullptr) {
                    zlink::samples::send_stream_push (
                      *opponent_stream, opponent_codec, game_state_notify_t::packet_name,
                      game_state_notify_t{current_room_id, state.next_turn, state});
                }
            }
            log << "reply " << frame->name << '\n';
            log << "push " << game_state_notify_t::packet_name << '\n';
            ++handled;
        }
        inbound.close ();
    }
    log << "disconnect client\n";
    log << "shutdown server\n";
}

} // namespace zlink::samples::tictactoe
