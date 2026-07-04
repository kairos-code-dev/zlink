/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Actors/player_actor.hpp"
#include "../../../../../Configuration/sample_names.hpp"
#include "Notifications/game_notification_publisher.hpp"
#include "../../../../Domain/TicTacToe/tictactoe_match.hpp"

#include <zlink/framework.hpp>

#include <map>
#include <stdexcept>
#include <vector>

namespace zlink::samples::tictactoe
{

using namespace framework;
using framework::actor_ref_t;
using framework::message_t;

class tictactoe_game_spot_t : public spot_t, public tictactoe_match_t
{
  public:
    tictactoe_game_spot_t () : tictactoe_match_t ("") {}

    void configure (spot_context_t &context)
    {
        _context = context;
        context.handlers ().add_actor_request<&tictactoe_game_spot_t::place_mark> ();
        context.handlers ().add_actor_request<&tictactoe_game_spot_t::leave_game> ();
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

    spot_actor_join_response_t on_actor_join (const player_actor_t &actor,
                                              const message_t &request_message)
    {
        auto request = request_message.decode<tictactoe_game_join_req_t> ();
        if (request.player.actor_id.empty ()
            || request.player.level < sample_names_t::required_level) {
            return spot_actor_join_response_t::reject ();
        }
        players[actor.actor_id] = request.player;
        actor.apply_player (request.player);
        return spot_actor_join_response_t::accept (
          join (actor.actor_id, join_game_req_t{request.room_id, request.player}));
    }

    place_mark_res_t place_mark (const player_actor_t &actor,
                                 const spot_actor_request_context_t &context,
                                 const place_mark_req_t &request)
    {
        if (context.packet_name.empty ()) {
            throw std::runtime_error ("packet name is required");
        }
        auto state = place (actor.actor_id, request);
        game_state_notify_t state_notify{state.room_id, state.next_turn, state};
        publisher.publish_game_state (state_notify);
        send_to_other_actors (actor.actor_id, state_notify);
        if (state.status == tictactoe_status_t::won || state.status == tictactoe_status_t::draw) {
            game_ended_notify_t ended_notify{state.room_id, state.winner, state.draw, state};
            publisher.publish_game_ended (ended_notify);
            send_to_other_actors (actor.actor_id, ended_notify);
            publish_win_milestone (actor, state);
        }
        return {state};
    }

    place_mark_res_t leave_game (const player_actor_t &actor,
                                 const spot_actor_request_context_t &,
                                 const leave_game_req_t &request)
    {
        if (request.room_id != snapshot ().room_id) {
            throw std::runtime_error ("LeaveGameReq room id does not match the joined room.");
        }
        actor.mark_for_destroy_after_room_leave ();
        (void) _context.leave_actor (actor_ref_for (actor), const_cast<player_actor_t &> (actor));
        return {snapshot ()};
    }

    void on_actor_joined (const player_actor_t &actor)
    {
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
        publisher.publish_player_joined (notify);
        send_to_other_actors (actor.actor_id, notify);
    }

    void onLeaveActor (const player_actor_t &actor)
    {
        actors.erase (actor.actor_id);
        players.erase (actor.actor_id);
        const auto &state = snapshot ();
        game_ended_notify_t notify{state.room_id, state.winner, state.draw, state};
        publisher.publish_game_ended (notify);
        send_to_other_actors (actor.actor_id, notify);
    }

    void onDisconnectActor (const player_actor_t &actor) { actor.mark_disconnected (); }

    game_notification_publisher_t publisher;

  private:
    template <typename TNotify>
    void send_to_other_actors (const std::string &source_actor_id, const TNotify &notify)
    {
        for (auto &[actor_id, actor] : actors) {
            if (actor_id == source_actor_id || actor == nullptr) {
                continue;
            }
            actor->context.bound_session ().send (notify).submit ();
        }
    }

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
    std::map<std::string, player_actor_t *> actors;
    std::map<std::string, player_info_t> players;
};

} // namespace zlink::samples::tictactoe
