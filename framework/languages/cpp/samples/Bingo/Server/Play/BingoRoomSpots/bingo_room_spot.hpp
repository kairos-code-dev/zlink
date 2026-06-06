/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Actors/player_actor.hpp"
#include "bingo_notification_publisher.hpp"
#include "bingo_room.hpp"

#include <zlink/framework.hpp>

#include <stdexcept>

namespace zlink::samples::bingo
{

class bingo_room_spot_t : public zlink::framework::spot_t, public bingo_room_t
{
  public:
    using bingo_room_t::bingo_room_t;

    void configure (zlink::framework::spot_context_t &context)
    {
        context.handlers ()
          .add_actor_join<&bingo_room_spot_t::join_actor> ()
          .add_actor_packet<&bingo_room_spot_t::start_game> ()
          .add_post_actor_joined<&bingo_room_spot_t::on_actor_joined> ()
          .add_actor_left<&bingo_room_spot_t::on_actor_left> ();
    }

    bingo_room_join_res_t join_actor (const player_actor_t &actor, const bingo_room_join_req_t &request)
    {
        const auto actor_id = actor.actor.actor_id.empty () ? request.actor_id : actor.actor.actor_id;
        const auto display_name = actor.display_name.empty () ? request.display_name : actor.display_name;
        join (actor_id, display_name);
        return {snapshot ()};
    }

    start_bingo_game_res_t start_game (const player_actor_t &,
                                       const zlink::framework::spot_actor_request_context_t &context,
                                       const start_bingo_game_req_t &)
    {
        if (context.packet_name.empty ()) {
            throw std::runtime_error ("packet name is required");
        }
        return {start ()};
    }

    void on_actor_joined (const player_actor_t &actor, const zlink::framework::spot_actor_change_result_t &)
    {
        const auto &state = snapshot ();
        publisher.publish_joined ({state.room_id, actor.actor.actor_id, actor.actor.actor_id, 0,
                                   actor.actor.actor_id == state.host_actor_id, state});
    }

    void on_actor_left (const player_actor_t &actor, const zlink::framework::spot_actor_change_result_t &)
    {
        leave (actor.actor.actor_id);
    }

    bingo_notification_publisher_t publisher;
};

} // namespace zlink::samples::bingo
