/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "tictactoe_player_client.hpp"

#include <zlink/http_client.hpp>

#include <fstream>

namespace zlink::samples::tictactoe
{

inline constexpr const char *tictactoe_client_log_file = "tictactoe-client.log";

class tictactoe_client_t
{
  public:
    tictactoe_client_run_result_t run (const tictactoe_client_options_t &options)
    {
        std::ofstream log (tictactoe_client_log_file, std::ios::trunc);
        tictactoe_client_options_t run_options = options;

        auto http_client = zlink::http_client::client_t::create ()
                             .base_url (run_options.api_http_endpoint)
                             .json ()
                             .timeout (std::chrono::milliseconds (500))
                             .build ();
        bool http_ready = false;
        for (int attempt = 0; attempt < 100 && !http_ready; ++attempt) {
            auto ready = http_client.post ("/games")
                           .body (create_game_req_t{run_options.x_actor_id})
                           .submit<create_game_res_t> ()
                           .result ();
            http_ready = ready.has_value () && !ready.value ().body.play_endpoint.empty ();
            if (http_ready) {
                run_options.game_name = ready.value ().body.room_id;
                run_options.play_endpoint = ready.value ().body.play_endpoint;
                log << "client http POST /games completed=1 room=" << run_options.game_name << '\n';
            } else {
                std::this_thread::sleep_for (std::chrono::milliseconds (10));
            }
        }

        auto x = tictactoe_player_client_t::connect (run_options.x_actor_id, run_options);
        auto o = tictactoe_player_client_t::connect (run_options.o_actor_id, run_options);

        tictactoe_client_run_result_t result;
        result.connected = x.connected () && o.connected ();
        log << "client connected " << run_options.x_actor_id << " completed=" << x.connected ()
            << '\n';
        log << "client connected " << run_options.o_actor_id << " completed=" << o.connected ()
            << '\n';
        result.game_name = run_options.game_name;
        result.api_endpoint = run_options.api_http_endpoint;
        result.play_endpoint = run_options.play_endpoint;
        result.http_game_created = http_ready;
        result.requests.push_back (x.authenticate ());
        log << "client request " << result.requests.back ().packet_name
            << " completed=" << result.requests.back ().completed << '\n';
        result.requests.push_back (o.authenticate ());
        log << "client request " << result.requests.back ().packet_name
            << " completed=" << result.requests.back ().completed << '\n';
        result.requests.push_back (x.join_game (run_options.game_name));
        log << "client request " << result.requests.back ().packet_name
            << " completed=" << result.requests.back ().completed << '\n';
        result.requests.push_back (o.join_game (run_options.game_name));
        log << "client request " << result.requests.back ().packet_name
            << " completed=" << result.requests.back ().completed << '\n';
        result.requests.push_back (x.place_mark (run_options.game_name, 0));
        log << "client request " << result.requests.back ().packet_name
            << " completed=" << result.requests.back ().completed << '\n';
        result.requests.push_back (o.place_mark (run_options.game_name, 3));
        log << "client request " << result.requests.back ().packet_name
            << " completed=" << result.requests.back ().completed << '\n';
        result.requests.push_back (x.place_mark (run_options.game_name, 1));
        log << "client request " << result.requests.back ().packet_name
            << " completed=" << result.requests.back ().completed << '\n';
        result.requests.push_back (o.place_mark (run_options.game_name, 4));
        log << "client request " << result.requests.back ().packet_name
            << " completed=" << result.requests.back ().completed << '\n';
        result.requests.push_back (x.place_mark (run_options.game_name, 2));
        log << "client request " << result.requests.back ().packet_name
            << " completed=" << result.requests.back ().completed << '\n';

        x.dispatch ();
        o.dispatch ();
        result.player_joined_notifications =
          x.notifications ().players.size () + o.notifications ().players.size ();
        result.game_state_notifications =
          x.notifications ().states.size () + o.notifications ().states.size ();
        result.game_ended_notifications =
          x.notifications ().ended.size () + o.notifications ().ended.size ();
        log << "client push " << player_joined_notify_t::packet_name
            << " count=" << result.player_joined_notifications << '\n';
        log << "client push " << game_state_notify_t::packet_name
            << " count=" << result.game_state_notifications << '\n';
        log << "client push " << game_ended_notify_t::packet_name
            << " count=" << result.game_ended_notifications << '\n';
        log << "client game completed\n";

        x.close ();
        o.close ();
        return result;
    }
};

} // namespace zlink::samples::tictactoe
