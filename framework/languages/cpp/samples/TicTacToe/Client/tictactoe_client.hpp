/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "tictactoe_player_client.hpp"

#include <zlink/http_client.hpp>

namespace zlink::samples::tictactoe
{

class tictactoe_client_t
{
  public:
    tictactoe_client_run_result_t run (const tictactoe_client_options_t &options)
    {
        tictactoe_client_options_t run_options = options;

        auto http_client = zlink::http_client::client_t::create ()
                             .base_url (run_options.api_http_endpoint)
                             .json ()
                             .timeout (std::chrono::milliseconds (500))
                             .build ();
        bool http_ready = false;
        for (int attempt = 0; attempt < 100 && !http_ready; ++attempt) {
            auto ready = http_client.post ("/games")
                           .body (create_match_req_t{run_options.x_actor_id})
                           .submit<create_match_res_t> ()
                           .result ();
            http_ready = ready.has_value () && !ready.value ().body.play_endpoint.empty ();
            if (http_ready) {
                run_options.game_name = ready.value ().body.match_id;
                run_options.play_endpoint = ready.value ().body.play_endpoint;
            } else {
                std::this_thread::sleep_for (std::chrono::milliseconds (10));
            }
        }

        auto x = tictactoe_player_client_t::connect (run_options.x_actor_id, run_options);
        auto o = tictactoe_player_client_t::connect (run_options.o_actor_id, run_options);

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
        result.turn_changed_notifications = x.notifications ().turns.size () + o.notifications ().turns.size ();
        result.game_ended_notifications = x.notifications ().ended.size () + o.notifications ().ended.size ();

        x.close ();
        o.close ();
        return result;
    }
};

} // namespace zlink::samples::tictactoe
