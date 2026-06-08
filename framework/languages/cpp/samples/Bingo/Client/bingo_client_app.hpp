/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "bingo_player_client.hpp"

#include <fstream>
#include <vector>

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

        auto player_one = bingo_player_client_t::connect ("player-1", run_options);
        auto player_two = bingo_player_client_t::connect ("player-2", run_options);

        bingo_client_run_result_t result;
        result.connected = player_one.connected () && player_two.connected ();
        log << "client connected " << player_one.actor_id () << '\n';
        result.requests.push_back (player_one.authenticate ());
        log << "client request " << result.requests.back ().packet_name
            << " completed=" << result.requests.back ().completed << '\n';

        log << "client connected " << player_two.actor_id () << '\n';
        result.requests.push_back (player_two.authenticate ());
        log << "client request " << result.requests.back ().packet_name
            << " completed=" << result.requests.back ().completed << '\n';

        result.requests.push_back (player_one.match ());
        log << "client request " << result.requests.back ().packet_name
            << " completed=" << result.requests.back ().completed << '\n';

        result.requests.push_back (player_two.match ());
        log << "client request " << result.requests.back ().packet_name
            << " completed=" << result.requests.back ().completed << '\n';

        const std::string room_id = "two-player-room-1";
        result.requests.push_back (player_one.submit_card (room_id, {1, 2, 3, 4, 5, 6, 7, 8, 9}));
        log << "client request " << result.requests.back ().packet_name
            << " completed=" << result.requests.back ().completed << '\n';

        result.requests.push_back (
          player_two.submit_card (room_id, {7, 8, 9, 10, 11, 12, 13, 14, 15}));
        log << "client request " << result.requests.back ().packet_name
            << " completed=" << result.requests.back ().completed << '\n';

        player_one.dispatch ();
        player_two.dispatch ();
        result.player_joined_notifications =
          player_one.notifications ().joined.size () + player_two.notifications ().joined.size ();
        result.started_notifications =
          player_one.notifications ().started.size () + player_two.notifications ().started.size ();
        result.drawn_notifications =
          player_one.notifications ().drawn.size () + player_two.notifications ().drawn.size ();
        result.ended_notifications =
          player_one.notifications ().ended.size () + player_two.notifications ().ended.size ();
        player_one.close ();
        player_two.close ();
        log << "client push " << player_joined_notify_t::packet_name
            << " count=" << result.player_joined_notifications << '\n';
        log << "client push " << game_started_notify_t::packet_name
            << " count=" << result.started_notifications << '\n';
        log << "client push " << number_drawn_notify_t::packet_name
            << " count=" << result.drawn_notifications << '\n';
        log << "client push " << game_ended_notify_t::packet_name
            << " count=" << result.ended_notifications << '\n';
        log << "client game completed\n";
        return result;
    }
};

} // namespace zlink::samples::bingo
