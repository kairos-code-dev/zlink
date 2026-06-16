/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Actors/player_actor.hpp"
#include "../../../../Configuration/sample_names.hpp"
#include "../Notifications/game_notification_publisher.hpp"
#include "../../../Domain/TicTacToe/tictactoe_match.hpp"

#include <zlink/framework.hpp>

#include <map>
#include <stdexcept>
#include <vector>

namespace zlink::samples::tictactoe
{

class tictactoe_game_spot_t : public zlink::framework::spot_t, public tictactoe_match_t
{
  public:
    tictactoe_game_spot_t () : tictactoe_match_t ("") {}

    void configure (zlink::framework::spot_context_t &context)
    {
        _context = context;
        context.handlers ().add_actor_packet<&tictactoe_game_spot_t::place_mark> ();
    }

    zlink::framework::spot_create_response_t on_create (const zlink::message_t &request)
    {
        static_cast<tictactoe_match_t &> (*this) = tictactoe_match_t (request.to_string ());
        return zlink::framework::spot_create_response_t::accept ();
    }

    zlink::framework::spot_actor_join_response_t
    on_actor_join (const player_actor_t &actor, const zlink::message_t &request_message)
    {
        join_game_req_t request;
        from_stream_payload (request_message, request);
        return zlink::framework::spot_actor_join_response_t::accept (
          to_stream_payload (join (actor.actor_id, request)));
    }

    place_mark_res_t place_mark (const player_actor_t &actor,
                                 const zlink::framework::spot_actor_request_context_t &context,
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
            leave_finished_actors (state);
        }
        return {state};
    }

    void onJoinActor (const player_actor_t &actor)
    {
        actors[actor.actor_id] = const_cast<player_actor_t *> (&actor);
        const auto &state = snapshot ();
        player_joined_notify_t notify{
          state.room_id,
          actor.actor_id,
          actor.actor_id == state.x_actor_id ? tictactoe_marks_t::x : tictactoe_marks_t::o,
          state};
        publisher.publish_player_joined (notify);
        send_to_other_actors (actor.actor_id, notify);
    }

    void onLeaveActor (const player_actor_t &actor)
    {
        actors.erase (actor.actor_id);
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
            (void) actor->context.bound_session ().send (notify).async ().result ();
        }
    }

    void leave_finished_actors (const tictactoe_state_t &state)
    {
        if (cleanup_started
            || (state.status != tictactoe_status_t::won
                && state.status != tictactoe_status_t::draw)) {
            return;
        }
        cleanup_started = true;
        std::vector<player_actor_t *> leaving;
        for (auto &[_, actor] : actors) {
            leaving.push_back (actor);
        }
        for (auto *actor : leaving) {
            actor->mark_for_destroy_after_room_leave ();
            (void) _context.leaveActor (actor_ref_for (*actor), *actor);
        }
    }

    static zlink::framework::actor_ref_t actor_ref_for (const player_actor_t &actor)
    {
        return zlink::framework::actor_ref_t (
          zlink::framework::node_rid_t::from_string (sample_names_t::spot_node),
          sample_names_t::actor_type, actor.actor_id, actor.generation);
    }

    zlink::framework::spot_context_t _context;
    std::map<std::string, player_actor_t *> actors;
    bool cleanup_started = false;
};

} // namespace zlink::samples::tictactoe
