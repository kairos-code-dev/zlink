/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "bingo_player_client.hpp"

#include <array>

namespace zlink::samples::bingo
{

class bingo_client_app_t
{
  public:
    bingo_client_run_result_t run (const bingo_client_options_t &options)
    {
        bingo_client_options_t run_options = options;

        std::array<bingo_player_client_t, 4> clients{bingo_player_client_t::connect ("player-1", run_options),
                                                     bingo_player_client_t::connect ("player-2", run_options),
                                                     bingo_player_client_t::connect ("player-3", run_options),
                                                     bingo_player_client_t::connect ("player-4", run_options)};

        bingo_client_run_result_t result;
        result.connected = true;
        for (auto &client : clients) {
            result.connected = result.connected && client.connected ();
            result.requests.push_back (client.authenticate ());
        }

        result.requests.push_back (clients[0].match ());
        result.requests.push_back (clients[1].match ());
        result.requests.push_back (clients[2].match ());
        result.requests.push_back (clients[3].match ());
        result.requests.push_back (clients[0].start ("room-1"));
        result.sends.push_back (clients[3].leave ("room-1"));

        for (auto &client : clients) {
            client.dispatch ();
            result.player_joined_notifications += client.notifications ().joined.size ();
            result.started_notifications += client.notifications ().started.size ();
            result.drawn_notifications += client.notifications ().drawn.size ();
            result.ended_notifications += client.notifications ().ended.size ();
            client.close ();
        }
        return result;
    }
};

} // namespace zlink::samples::bingo
