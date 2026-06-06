/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/Actors/player_actor.hpp"
#include "game_notification_publisher.hpp"
#include "tictactoe_match_room.hpp"

#include <zlink/framework.hpp>

#include <stdexcept>

namespace zlink::samples::tictactoe
{

class tictactoe_game_spot_t : public zlink::framework::spot_t, public tictactoe_match_room_t
{
  public:
    using tictactoe_match_room_t::tictactoe_match_room_t;

    void configure (zlink::framework::spot_context_t &context)
    {
        context.handlers ().add_actor_packet<&tictactoe_game_spot_t::place_mark> ();
    }

    zlink::framework::spot_actor_join_response_t on_actor_join (const player_actor_t &,
                                                                const zlink::message_t &request_message)
    {
        return zlink::framework::spot_actor_join_response_t::accept (
          zlink::message_t::from_json (join (request_message.parse_json<join_match_req_t> ())));
    }

    place_mark_res_t place_mark (const player_actor_t &,
                                 const zlink::framework::spot_actor_request_context_t &context,
                                 const place_mark_req_t &request)
    {
        if (context.packet_name.empty ()) {
            throw std::runtime_error ("packet name is required");
        }
        return {place (request)};
    }

    void on_post_actor_joined (const player_actor_t &actor)
    {
        const auto &state = snapshot ();
        publisher.publish_opponent_joined (
          {state.match_id, actor.actor_id, actor.actor_id == state.x_actor_id ? "X" : "O", state});
    }

    void on_actor_left (const player_actor_t &)
    {
        const auto &state = snapshot ();
        publisher.publish_game_ended ({state.match_id, state.winner_actor_id, state.draw, state});
    }

    game_notification_publisher_t publisher;
};

} // namespace zlink::samples::tictactoe
