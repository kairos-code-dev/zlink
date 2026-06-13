/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Actors/player_actor.hpp"
#include "../../../../Configuration/sample_names.hpp"
#include "../Notifications/bingo_notification_publisher.hpp"
#include "../../../Domain/Bingo/bingo_room_game.hpp"

#include <zlink/framework.hpp>

#include <map>
#include <stdexcept>
#include <vector>

namespace zlink::samples::bingo
{

class bingo_room_spot_t : public zlink::framework::spot_t, public bingo_room_game_t
{
  public:
    using bingo_room_game_t::bingo_room_game_t;

    void configure (zlink::framework::spot_context_t &context)
    {
        _context = context;
        context.handlers ().add_actor_packet<&bingo_room_spot_t::submit_card> ();
    }

    zlink::framework::spot_actor_join_response_t
    on_actor_join (const player_actor_t &actor, const zlink::message_t &request_message)
    {
        bingo_room_join_req_t request;
        from_stream_payload (request_message, request);
        const auto actor_id =
          actor.actor.actor_id.empty () ? request.actor_id : actor.actor.actor_id;
        const auto display_name =
          actor.display_name.empty () ? request.display_name : actor.display_name;
        set_room_id_if_empty (request.room_id);
        join (actor_id, display_name);
        return zlink::framework::spot_actor_join_response_t::accept (
          to_stream_payload (bingo_room_join_res_t{snapshot ()}));
    }

    submit_bingo_card_res_t
    submit_card (const player_actor_t &actor,
                 const zlink::framework::spot_actor_request_context_t &context,
                 const submit_bingo_card_req_t &request)
    {
        if (context.packet_name.empty ()) {
            throw std::runtime_error ("packet name is required");
        }
        (void) bingo_room_game_t::submit_card (actor.actor.actor_id, request.card);
        if (should_draw ()) {
            while (const auto drawn = draw_next ()) {
                publisher.publish_drawn (*drawn);
                if (!drawn->state.winners.empty ()) {
                    publisher.publish_ended ({drawn->state});
                    leave_finished_actors ();
                    break;
                }
            }
        }
        return submit_bingo_card_res_t{snapshot ()};
    }

    void onJoinActor (const player_actor_t &actor)
    {
        actors[actor.actor.actor_id] = const_cast<player_actor_t *> (&actor);
        const auto &state = snapshot ();
        if (state.players.size () == 2) {
            publisher.publish_started ({state});
        }
    }

    void onLeaveActor (const player_actor_t &actor)
    {
        actors.erase (actor.actor.actor_id);
        leave (actor.actor.actor_id);
    }

    void onDisconnectActor (const player_actor_t &actor) { actor.mark_disconnected (); }

    bingo_notification_publisher_t publisher;

  private:
    void leave_finished_actors ()
    {
        if (cleanup_started || snapshot ().winners.empty ()) {
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
          zlink::framework::node_rid_t::from_string (sample_names_t::room_spot_node),
          sample_names_t::player_actor_type, actor.actor.actor_id, actor.actor.generation);
    }

    zlink::framework::spot_context_t _context;
    std::map<std::string, player_actor_t *> actors;
    bool cleanup_started = false;
};

} // namespace zlink::samples::bingo
