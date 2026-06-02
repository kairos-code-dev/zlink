/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "Actors/player_actor.hpp"
#include "Configuration/sample_names.hpp"
#include "Configuration/sample_topology.hpp"
#include "../Client/session_actor_notification_inbox.hpp"
#include "../Server/Play/EntrySpot/tictactoe_entry_spot.hpp"
#include "../Server/Play/GameSpots/place_mark_handler.hpp"

#include <zlink/framework.hpp>
#include <zlink/stream_connector.hpp>

#include <string>

namespace zlink::samples::tictactoe
{

inline void
configure_registry_host (zlink::framework::zlink_builder_t &zlink)
{
  zlink.registry ([](zlink::framework::registry_builder_t &registry) {
    registry.bind ("tcp://127.0.0.1:48101", "tcp://127.0.0.1:48102");
  });
}

inline void
configure_api_host (zlink::framework::zlink_builder_t &zlink,
                    const sample_topology_t &topology)
{
  zlink.node ("tictactoe-api")
    .channel (sample_names_t::api_channel,
              [&](zlink::framework::channel_builder_t &channel) {
    channel.enable_server (
      [&](zlink::framework::capability_builder_t &server) {
        server.bind (topology.api_endpoint);
      });
    channel.enable_client (
      [&](zlink::framework::capability_builder_t &client) {
        client.connect (topology.play_endpoint);
      });
  });
}

inline void
configure_play_host (zlink::framework::zlink_builder_t &zlink,
                     const sample_topology_t &topology)
{
  zlink.node ("tictactoe-play")
    .channel (sample_names_t::play_channel,
              [&](zlink::framework::channel_builder_t &channel) {
    channel.enable_server (
      [&](zlink::framework::capability_builder_t &server) {
        server.bind (topology.play_endpoint);
      });
  })
    .spot_node (sample_names_t::spot_node,
                [&](zlink::framework::spot_node_builder_t &spot_node) {
    spot_node.bind (topology.spot_endpoint)
      .enable_actor_gateway ()
      .add_entry_spot<entry_spot_t> ()
      .add_spot<tictactoe_match_room_t> (sample_names_t::match_spot)
      .add_actor_factory<player_actor_factory_t> (sample_names_t::actor_type);
  });
}

inline void
configure_session_host (zlink::framework::zlink_builder_t &zlink,
                        const sample_topology_t &topology)
{
  zlink.node ("tictactoe-session")
    .stream (sample_names_t::stream_name,
             [&](zlink::framework::stream_builder_t &stream) {
    stream.bind (topology.stream_endpoint)
      .packet_session ("client-session")
      .attach_actor_gateway (sample_names_t::spot_node);
  });
}

inline tictactoe_state_t
play_finished_game (tictactoe_match_room_t &room,
                    notification_inbox_t &inbox,
                    const std::string &match_id)
{
  place_mark_handler_t handler (room);
  auto state = handler.handle ({ match_id, sample_names_t::x_actor_id, 0 }).state;
  inbox.turn_changed ({ match_id, state.turn_actor_id, state });
  state = handler.handle ({ match_id, sample_names_t::o_actor_id, 3 }).state;
  inbox.turn_changed ({ match_id, state.turn_actor_id, state });
  state = handler.handle ({ match_id, sample_names_t::x_actor_id, 1 }).state;
  inbox.turn_changed ({ match_id, state.turn_actor_id, state });
  state = handler.handle ({ match_id, sample_names_t::o_actor_id, 4 }).state;
  inbox.turn_changed ({ match_id, state.turn_actor_id, state });
  state = handler.handle ({ match_id, sample_names_t::x_actor_id, 2 }).state;
  inbox.game_ended ({ match_id, state.winner_actor_id, state.draw, state });
  return state;
}

} // namespace zlink::samples::tictactoe
