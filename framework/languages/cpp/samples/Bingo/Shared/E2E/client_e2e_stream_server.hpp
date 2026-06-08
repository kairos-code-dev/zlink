/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/sample.hpp"
#include "../../../Shared/stream_frame_server.hpp"

#include <zlink/Contracts/Sockets/stream_socket.hpp>

#include <fstream>
#include <string>

namespace zlink::samples::bingo
{

inline void run_client_e2e_stream_server (zlink::stream_socket_t &server,
                                          const std::string &endpoint)
{
    std::ofstream log ("bingo-server.log", std::ios::trunc);
    log << "bind " << endpoint << '\n';
    log << "monitor stream ready\n";
    log << "monitor event stream_ready\n";
    log.flush ();
    int handled = 0;
    std::string buffer;
    while (handled < 6) {
        zlink::received_t inbound;
        if (server.recv (inbound) != 0) {
            log << "recv failed index=" << handled << '\n';
            return;
        }
        buffer += inbound.parts ().empty () ? std::string{} : inbound.parts ()[0].to_string ();
        while (auto frame = zlink::samples::try_read_stream_frame (buffer)) {
            log << "recv " << frame->name << '\n';
            if (frame->kind == zlink::stream_connector::message_kind_t::request) {
                bingo_room_state_t state;
                state.room_id = "two-player-room-1";
                if (frame->name == authenticate_req_t::packet_name) {
                    zlink::samples::send_stream_reply (inbound, *frame,
                                                       authenticate_res_t{"player", "Player"});
                } else if (frame->name == match_bingo_req_t::packet_name) {
                    state.status = "waiting";
                    state.host_actor_id = "player-1";
                    state.can_start = true;
                    log << "actor relay " << state.host_actor_id << '\n';
                    zlink::samples::send_stream_reply (inbound, *frame,
                                                       match_bingo_res_t{state.room_id, state});
                } else if (frame->name == submit_bingo_card_req_t::packet_name) {
                    state.status = "running";
                    state.draw_seq = 1;
                    state.last_drawn_number = 1;
                    state.drawn_numbers = {1};
                    state.winners = {"player-1"};
                    zlink::samples::send_stream_frames (
                      inbound,
                      {zlink::samples::make_stream_reply_frame (
                         *frame, zlink::message_t::from_json (submit_bingo_card_res_t{state})),
                       zlink::samples::make_stream_push_frame (
                         player_joined_notify_t::packet_name,
                         zlink::message_t::from_json (player_joined_notify_t{
                           state.room_id, "player-2", "Player 2", 2, false, state})),
                       zlink::samples::make_stream_push_frame (
                         game_started_notify_t::packet_name,
                         zlink::message_t::from_json (game_started_notify_t{state})),
                       zlink::samples::make_stream_push_frame (
                         number_drawn_notify_t::packet_name,
                         zlink::message_t::from_json (
                           number_drawn_notify_t{state.room_id, 1, 7, state})),
                       zlink::samples::make_stream_push_frame (
                         game_ended_notify_t::packet_name,
                         zlink::message_t::from_json (game_ended_notify_t{state}))});
                    log << "push " << game_started_notify_t::packet_name << '\n';
                    log << "push " << player_joined_notify_t::packet_name << '\n';
                    log << "push " << number_drawn_notify_t::packet_name << '\n';
                    log << "push " << game_ended_notify_t::packet_name << '\n';
                } else {
                    zlink::samples::send_stream_reply (inbound, *frame,
                                                       match_bingo_res_t{state.room_id, state});
                }
                log << "reply " << frame->name << '\n';
            } else {
                log << "send-only " << frame->name << '\n';
            }
            ++handled;
        }
        inbound.close ();
    }
    log << "disconnect client\n";
    log << "shutdown server\n";
}

} // namespace zlink::samples::bingo
