/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../Actors/player_actor.hpp"
#include "../../../../../Configuration/sample_names.hpp"
#include "Notifications/game_notification_publisher.hpp"
#include "../../../../Domain/TicTacToe/tictactoe_match.hpp"

#include <zlink/framework.hpp>

#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace zlink::samples::tictactoe
{

using namespace framework;
using framework::actor_ref_t;
using framework::message_t;

class tictactoe_game_spot_t;

class tictactoe_game_timer_handler_t
{
  public:
    task_t<void> handle (tictactoe_game_spot_t &spot, const timer_tick_t &tick) const;
};

class tictactoe_game_spot_t : public spot_t, public tictactoe_match_t
{
  private:
    std::map<std::string, player_actor_t *> actors;
    game_notification_publisher_t publisher{actors};

  public:
    tictactoe_game_spot_t () : tictactoe_match_t ("") {}

    void configure (spot_context_t &context)
    {
        _context = context;
        context.handlers ().add_actor_request<&tictactoe_game_spot_t::place_mark> ();
        context.handlers ().add_actor_send<&tictactoe_game_spot_t::leave_game> ();
    }

    spot_create_response_t on_create (const message_t &)
    {
        auto room_id = std::string (_context.spot_rid ().value ());
        if (const auto separator = room_id.rfind (':');
            separator != std::string::npos && separator + 1 < room_id.size ()) {
            room_id = room_id.substr (separator + 1);
        }
        static_cast<tictactoe_match_t &> (*this) = tictactoe_match_t (room_id);
        return spot_create_response_t::accept ();
    }

    void on_initialize ()
    {
        using namespace std::chrono_literals;
        _game_timer =
          _context.add_timer<tictactoe_game_timer_handler_t> ("game-tick", 1s);
    }

    void on_closing () { _game_timer.cancel (); }

    spot_actor_join_response_t on_actor_join (std::string_view actor_id,
                                              const message_t &request_message)
    {
        auto request = request_message.decode<tictactoe_game_join_req_t> ();
        if (request.player.actor_id.empty ()
            || request.player.level < sample_names_t::required_level) {
            return spot_actor_join_response_t::reject ();
        }
        auto projected = static_cast<const tictactoe_match_t &> (*this);
        auto response = projected.join (std::string (actor_id), request.room_id);
        _pending_joins[std::string (actor_id)] = request;
        return spot_actor_join_response_t::accept (
          std::move (response));
    }

    place_mark_res_t place_mark (const player_actor_t &actor,
                                 const spot_actor_request_context_t &context,
                                 const place_mark_req_t &request);

    void leave_game (const player_actor_t &actor,
                     const spot_actor_send_context_t &,
                     const leave_game_req_t &request);

    void on_actor_joined (const player_actor_t &actor)
    {
        const auto pending = _pending_joins.find (actor.actor_id);
        if (pending == _pending_joins.end ()) {
            throw std::runtime_error ("accepted tic-tac-toe actor admission is missing");
        }
        const auto request = pending->second;
        _pending_joins.erase (pending);
        players[actor.actor_id] = request.player;
        actor.apply_player (request.player);
        (void) join (actor.actor_id, request.room_id);
        actors[actor.actor_id] = const_cast<player_actor_t *> (&actor);
        const auto &state = snapshot ();
        player_joined_notify_t notify{
            state.room_id,
            actor.actor_id,
            players[actor.actor_id].display_name,
            players[actor.actor_id].level,
            actor.actor_id == state.x_actor_id ? tictactoe_marks_t::x : tictactoe_marks_t::o,
            state
        };
        publisher.publish (notify, actor.actor_id);

        game_state_notify_t state_notify{state.room_id, state.next_turn, state};
        publisher.publish (state_notify, actor.actor_id);
    }

    void on_leave_actor (const player_actor_t &actor)
    {
        actors.erase (actor.actor_id);
        players.erase (actor.actor_id);
        const auto &state = snapshot ();
        game_ended_notify_t notify{state.room_id, state.winner, state.draw, state};
        publisher.publish (notify, actor.actor_id);
    }

    void on_disconnect_actor (const player_actor_t &actor) { actor.mark_disconnected (); }

  private:
    friend class tictactoe_game_timer_handler_t;

    task_t<void> handle_game_tick (const timer_tick_t &);

    void publish_win_milestone (const player_actor_t &actor, const tictactoe_state_t &state)
    {
        if (state.status != tictactoe_status_t::won || state.winner != actor.actor_id) {
            return;
        }
        auto player = players[actor.actor_id];
        player.wins += 1;
        players[actor.actor_id] = player;
        actor.apply_player (player);
        if (player.wins == 100) {
            const auto milestone_event = player_win_milestone_msg_t{
              state.room_id, player.actor_id, player.display_name, player.wins};
            _context.publish (sample_names_t::player_milestone_topic, milestone_event).submit ();
        }
    }

    static actor_ref_t actor_ref_for (const player_actor_t &actor)
    {
        return actor_ref_t (node_rid_t::from_string (actor.node_rid.empty ()
                                                       ? std::string (sample_names_t::spot_node)
                                                       : actor.node_rid),
                            sample_names_t::actor_type, actor.actor_id, actor.generation);
    }

    spot_context_t _context;
    std::map<std::string, player_info_t> players;
    std::map<std::string, tictactoe_game_join_req_t> _pending_joins;
    framework::timer_t _game_timer;
};

} // namespace zlink::samples::tictactoe

#include "Handlers/play_actor_leave_game_handler.hpp"
#include "Handlers/play_actor_place_mark_handler.hpp"
#include "Handlers/tictactoe_game_timer_handler.hpp"
