/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../Configuration/sample_configuration.hpp"
#include "../Configuration/location_store.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/redis_room_route_store.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../../Shared/Contracts/messages.hpp"
#include "../host_support.hpp"
#include "../sample_log_dir.hpp"
#include "Infrastructure/ZLink/Handlers/ensure_player_actor_handler.hpp"
#include "Infrastructure/ZLink/Handlers/create_game_handler.hpp"
#include "Infrastructure/ZLink/Sessions/play_session.hpp"
#include "Infrastructure/ZLink/Actors/player_actor_transfer_adapter.hpp"
#include "Infrastructure/ZLink/Spots/EntrySpot/tictactoe_entry_spot.hpp"
#include "Infrastructure/ZLink/Spots/TicTacToeGameSpot/tictactoe_game_spot.hpp"
#include "Application/GameCreation/tictactoe_game_creator.hpp"

#include <memory>
#include <optional>

namespace zlink::samples::tictactoe
{

using namespace framework;

class play_server_host_factory_t
{
  public:
    static app_t build (const sample_topology_t &topology, bool auto_stop = true)
    {
        auto app = app_t::create ();
        configure (app, topology, auto_stop);
        return app;
    }

    static app_t &configure (app_t &app, const sample_topology_t &topology, bool auto_stop = true)
    {
        if (auto_stop) {
            app.add_hosted_service (std::make_unique<stop_after_start_service_t> (app));
        }
        app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
            options.configure_dispatch ()
              .message_flow (message_flow_log_mode_t::key_transitions)
              .trace_log_file (flow_log_path ("play-" + topology.selected_play_node_rid ()))
              .trace_label ("tictactoe-play-" + topology.selected_play_node_rid ());
            options.services ()
              .add_singleton<sample_topology_t> (std::make_unique<sample_topology_t> (topology))
              .add_singleton<redis_room_route_store_t, sample_topology_t> ()
              .add_singleton<tictactoe_game_creator_t, sample_topology_t,
                             redis_room_route_store_t> ();
            add_sample_location_store (options, topology);
            options.add_client_server_channel (sample_names_t::play_channel)
              .enable_server (topology.selected_play_endpoint ())
              .set_routing_id (zlink::routing_id_t::from (topology.selected_play_node_rid ()))
              .use_handler_group ("play");
            options.add_client_server_channel (sample_names_t::api_channel)
              .enable_client ();
            options.add_spot_mesh (sample_names_t::game_spot_node)
              .set_routing_id (zlink::routing_id_t::from (topology.selected_play_node_rid ()))
              .enable_router (topology.selected_play_spot_router_endpoint ())
              .connect_router (zlink::routing_id_t::from (topology.peer_play_node_rid ()),
                                topology.peer_play_spot_router_endpoint ())
              .enable_pub_sub (topology.selected_play_spot_endpoint ())
              .connect_peer_pub (topology.peer_play_spot_endpoint ())
              .add_entry_spot<tictactoe_entry_spot_t> ()
              .add_spot<tictactoe_game_spot_t> (sample_names_t::match_spot)
              .add_actor_factory<player_actor_factory_t> (sample_names_t::actor_type)
              .add_actor_transfer_adapter<player_actor_t, player_actor_transfer_adapter_t> (
                sample_names_t::actor_type)
              .add_spot_resolver (
                "redis-room-route",
                [topology] (spot_rid_t spot_rid) -> std::optional<spot_route_t> {
                    auto resolver_topology = topology;
                    redis_room_route_store_t routes (std::move (resolver_topology));
                    const auto route = routes.require (std::string (spot_rid.value ()));
                    return spot_route_t{
                      node_rid_t::from_string (route.owner_spot_node_rid),
                      spot_rid_t::from_string (route.room_id),
                      sample_names_t::match_spot};
                });
            options.add_stream_node (sample_names_t::stream_name)
              .bind (topology.selected_stream_endpoint ())
              .register_session<play_session_t> ();
            options.handlers ()
              .group ("play")
              .add<create_game_handler_t> ();
        });
        return app;
    }
};

} // namespace zlink::samples::tictactoe
