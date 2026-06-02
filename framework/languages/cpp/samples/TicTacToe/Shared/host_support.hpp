/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "Actors/player_actor.hpp"
#include "Configuration/sample_names.hpp"
#include "Configuration/sample_topology.hpp"
#include "../Client/session_actor_notification_inbox.hpp"
#include "../Server/Play/EntrySpot/tictactoe_entry_spot.hpp"
#include "../Server/Play/GameSpots/Handlers/place_mark_handler.hpp"

#include <zlink/framework.hpp>
#include <zlink/stream_connector.hpp>

#include <memory>
#include <string>

namespace zlink::samples::tictactoe
{

class stop_after_start_service_t final
  : public zlink::framework::hosted_service_t
{
public:
  explicit stop_after_start_service_t (zlink::framework::app_t &app)
    : _app (app)
  {
  }

  void start (zlink::framework::service_provider_t &) override
  {
    started = true;
    _app.stop ();
  }

  void stop () noexcept override { stopped = true; }

  bool started = false;
  bool stopped = false;

private:
  zlink::framework::app_t &_app;
};

inline zlink::framework::app_t &
add_sample_auto_stop (zlink::framework::app_t &app)
{
  app.add_hosted_service (std::make_unique<stop_after_start_service_t> (app));
  return app;
}

inline tictactoe_state_t
play_finished_game (tictactoe_match_room_t &room,
                    notification_inbox_t &inbox,
                    const std::string &match_id)
{
  place_mark_handler_t handler (room);
  zlink::framework::spot_actor_request_context_t context {
    place_mark_req_t::packet_name,
    "application/json",
    {},
    {} };
  player_actor_t actor { sample_names_t::x_actor_id };
  auto state =
    handler.handle (room, actor, context,
                    { match_id, sample_names_t::x_actor_id, 0 })
      .state;
  inbox.turn_changed (
    turn_changed_notify_t { match_id, state.turn_actor_id, state });
  actor.actor_id = sample_names_t::o_actor_id;
  state = handler.handle (room, actor, context,
                          { match_id, sample_names_t::o_actor_id, 3 })
            .state;
  inbox.turn_changed (
    turn_changed_notify_t { match_id, state.turn_actor_id, state });
  actor.actor_id = sample_names_t::x_actor_id;
  state = handler.handle (room, actor, context,
                          { match_id, sample_names_t::x_actor_id, 1 })
            .state;
  inbox.turn_changed (
    turn_changed_notify_t { match_id, state.turn_actor_id, state });
  actor.actor_id = sample_names_t::o_actor_id;
  state = handler.handle (room, actor, context,
                          { match_id, sample_names_t::o_actor_id, 4 })
            .state;
  inbox.turn_changed (
    turn_changed_notify_t { match_id, state.turn_actor_id, state });
  actor.actor_id = sample_names_t::x_actor_id;
  state = handler.handle (room, actor, context,
                          { match_id, sample_names_t::x_actor_id, 2 })
            .state;
  inbox.game_ended (
    game_ended_notify_t { match_id, state.winner_actor_id, state.draw, state });
  return state;
}

} // namespace zlink::samples::tictactoe
