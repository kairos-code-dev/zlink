/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Actors/player_actor.hpp"
#include "../Notifications/bingo_notification_publisher.hpp"
#include "../../../Domain/Bingo/bingo_room_game.hpp"

#include <zlink/framework.hpp>

#include <stdexcept>

namespace zlink::samples::bingo
{

class bingo_room_spot_t : public zlink::framework::spot_t, public bingo_room_game_t
{
  public:
    using bingo_room_game_t::bingo_room_game_t;

    void configure (zlink::framework::spot_context_t &context)
    {
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
        auto response = bingo_room_game_t::submit_card (actor.actor.actor_id, request.card);
        if (should_draw ()) {
            while (const auto drawn = draw_next ()) {
                publisher.publish_drawn (*drawn);
                if (!drawn->state.winners.empty ()) {
                    publisher.publish_ended ({drawn->state});
                    break;
                }
            }
        }
        return response;
    }

    void on_post_actor_joined (const player_actor_t &actor)
    {
        const auto &state = snapshot ();
        if (state.players.size () == 2) {
            publisher.publish_started ({state});
        }
    }

    void on_actor_left (const player_actor_t &actor) { leave (actor.actor.actor_id); }

    bingo_notification_publisher_t publisher;
};

} // namespace zlink::samples::bingo
