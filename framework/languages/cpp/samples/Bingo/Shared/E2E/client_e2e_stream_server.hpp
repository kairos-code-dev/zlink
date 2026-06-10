/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/sample.hpp"
#include "../../../Shared/stream_frame_server.hpp"

#include <zlink/Contracts/Sockets/stream_socket.hpp>

#include <fstream>
#include <optional>
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
    int submitted_cards = 0;
    std::optional<zlink::received_t> player_one_stream;
    std::optional<zlink::received_t> player_two_stream;
    zlink::stream_connector::codec_t player_one_codec = zlink::stream_connector::codec_t::protobuf;
    zlink::stream_connector::codec_t player_two_codec = zlink::stream_connector::codec_t::protobuf;
    std::string buffer;
    while (handled < 6) {
        zlink::received_t inbound;
        if (server.recv (inbound) != 0) {
            log << "recv failed index=" << handled << '\n';
            return;
        }
        buffer += inbound.parts ().empty () ? std::string{} : inbound.parts ()[0].to_string ();
        while (auto frame = zlink::samples::try_read_stream_frame (buffer)) {
            try {
            log << "recv " << frame->name << '\n';
            if (frame->kind == zlink::stream_connector::message_kind_t::request) {
                bingo_room_state_t state;
                state.room_id = "two-player-room-1";
            if (frame->name == authenticate_req_t::packet_name) {
                const auto request =
                  zlink::samples::parse_stream_payload<authenticate_req_t> (frame->payload);
                    if (request.access_token == "player-1") {
                        player_one_stream = inbound;
                        player_one_codec = frame->codec;
                    } else if (request.access_token == "player-2") {
                        player_two_stream = inbound;
                        player_two_codec = frame->codec;
                    }
                    zlink::samples::send_stream_reply (
                      inbound, *frame,
                      authenticate_res_t{request.access_token,
                                         "Player " + request.access_token.substr (7)});
                } else if (frame->name == match_bingo_req_t::packet_name) {
                    state.host_actor_id = "player-1";
                    if (handled < 3) {
                        state.status = "WaitingForPlayers";
                        state.players.push_back (
                          {"player-1", "Player 1", 1, true, {}, {}, 0});
                        log << "actor relay " << state.host_actor_id << '\n';
                        zlink::samples::send_stream_reply (
                          inbound, *frame, match_bingo_res_t{state.room_id, state});
                    } else {
                        state.status = "Running";
                        state.can_start = true;
                        state.players.push_back (
                          {"player-1", "Player 1", 1, true, {}, {}, 0});
                        state.players.push_back (
                          {"player-2", "Player 2", 2, false, {}, {}, 0});
                        zlink::samples::send_stream_reply (
                          inbound, *frame, match_bingo_res_t{state.room_id, state});
                        if (player_one_stream) {
                            zlink::samples::send_stream_push (
                              *player_one_stream, player_one_codec,
                              player_joined_notify_t::packet_name,
                              player_joined_notify_t{
                                state.room_id, "player-2", "Player 2", 2, false, state});
                            zlink::samples::send_stream_push (
                              *player_one_stream, player_one_codec,
                              game_started_notify_t::packet_name,
                              game_started_notify_t{state});
                        }
                        if (player_two_stream) {
                            zlink::samples::send_stream_push (
                              *player_two_stream, player_two_codec,
                              game_started_notify_t::packet_name,
                              game_started_notify_t{state});
                        }
                        log << "push " << player_joined_notify_t::packet_name << '\n';
                        log << "push " << game_started_notify_t::packet_name << '\n';
                    }
            } else if (frame->name == submit_bingo_card_req_t::packet_name) {
                const auto request = zlink::samples::parse_stream_payload<submit_bingo_card_req_t> (
                  frame->payload);
                    ++submitted_cards;
                    state.status = "Running";
                    state.players.push_back (
                      {"player-1", "Player 1", 1, true, {1, 2, 3, 4, 0, 6, 7, 8, 9},
                       {false, false, false, false, true, false, false, false, false}, 0});
                    state.players.push_back (
                      {"player-2", "Player 2", 2, false, request.card,
                       {false, false, false, false, true, false, false, false, false}, 0});
                    zlink::samples::send_stream_reply (inbound, *frame,
                                                       submit_bingo_card_res_t{state});
                    if (submitted_cards == 2) {
                        state.status = "Finished";
                        state.draw_seq = 1;
                        state.last_drawn_number = 7;
                        state.drawn_numbers = {7};
                        state.winners = {"player-1"};
                        const auto drawn =
                          number_drawn_notify_t{state.room_id, 1, 7, state};
                        const auto ended = game_ended_notify_t{state};
                        if (player_one_stream) {
                            zlink::samples::send_stream_push (
                              *player_one_stream, player_one_codec,
                              number_drawn_notify_t::packet_name, drawn);
                            zlink::samples::send_stream_push (
                              *player_one_stream, player_one_codec,
                              game_ended_notify_t::packet_name, ended);
                        }
                        if (player_two_stream) {
                            zlink::samples::send_stream_push (
                              *player_two_stream, player_two_codec,
                              number_drawn_notify_t::packet_name, drawn);
                            zlink::samples::send_stream_push (
                              *player_two_stream, player_two_codec,
                              game_ended_notify_t::packet_name, ended);
                        }
                        log << "push " << number_drawn_notify_t::packet_name << '\n';
                        log << "push " << game_ended_notify_t::packet_name << '\n';
                    }
                } else {
                    zlink::samples::send_stream_reply (inbound, *frame,
                                                       match_bingo_res_t{state.room_id, state});
                }
                log << "reply " << frame->name << '\n';
            } else {
                log << "send-only " << frame->name << '\n';
            }
            }
            catch (const std::exception &ex) {
                log << "frame failed " << frame->name << " error=" << ex.what () << '\n';
                log.flush ();
                return;
            }
            ++handled;
        }
        inbound.close ();
    }
    log << "disconnect client\n";
    log << "shutdown server\n";
}

} // namespace zlink::samples::bingo
