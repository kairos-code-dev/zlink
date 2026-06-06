/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "bingo_player_client.hpp"

#include <array>
#include <fstream>

namespace zlink::samples::bingo
{

inline constexpr const char *bingo_client_log_file = "bingo-client.log";

class bingo_client_app_t
{
  public:
    bingo_client_run_result_t run (const bingo_client_options_t &options)
    {
        std::ofstream log (bingo_client_log_file, std::ios::trunc);
        bingo_client_options_t run_options = options;

        std::array clients{bingo_player_client_t::connect ("player-1", run_options),
                           bingo_player_client_t::connect ("player-2", run_options),
                           bingo_player_client_t::connect ("player-3", run_options),
                           bingo_player_client_t::connect ("player-4", run_options)};

        bingo_client_run_result_t result;
        result.connected = true;
        for (auto &client : clients) {
            result.connected = result.connected && client.connected ();
            log << "client connected " << client.actor_id () << '\n';
            result.requests.push_back (client.authenticate ());
            log << "client request " << result.requests.back ().packet_name
                << " completed=" << result.requests.back ().completed << '\n';
        }

        result.requests.push_back (clients[0].match ());
        log << "client request " << result.requests.back ().packet_name
            << " completed=" << result.requests.back ().completed << '\n';
        result.requests.push_back (clients[1].match ());
        log << "client request " << result.requests.back ().packet_name
            << " completed=" << result.requests.back ().completed << '\n';
        result.requests.push_back (clients[2].match ());
        log << "client request " << result.requests.back ().packet_name
            << " completed=" << result.requests.back ().completed << '\n';
        result.requests.push_back (clients[3].match ());
        log << "client request " << result.requests.back ().packet_name
            << " completed=" << result.requests.back ().completed << '\n';
        result.requests.push_back (clients[0].start ("room-1"));
        log << "client request " << result.requests.back ().packet_name
            << " completed=" << result.requests.back ().completed << '\n';
        result.sends.push_back (clients[3].leave ("room-1"));
        log << "client send " << result.sends.back ().packet_name << " completed=" << result.sends.back ().completed
            << '\n';

        for (auto &client : clients) {
            client.dispatch ();
            result.player_joined_notifications += client.notifications ().joined.size ();
            result.started_notifications += client.notifications ().started.size ();
            result.drawn_notifications += client.notifications ().drawn.size ();
            result.ended_notifications += client.notifications ().ended.size ();
            client.close ();
        }
        log << "client push " << player_joined_notify_t::packet_name << " count=" << result.player_joined_notifications
            << '\n';
        log << "client push " << game_started_notify_t::packet_name << " count=" << result.started_notifications
            << '\n';
        log << "client push " << number_drawn_notify_t::packet_name << " count=" << result.drawn_notifications << '\n';
        log << "client push " << game_ended_notify_t::packet_name << " count=" << result.ended_notifications << '\n';
        log << "client game completed\n";
        return result;
    }
};

} // namespace zlink::samples::bingo
