/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/sample.hpp"
#include "Adapters/ZLink/Handlers/ensure_player_actor_handler.hpp"
#include "Adapters/ZLink/Spots/tictactoe_entry_spot.hpp"
#include "Adapters/ZLink/Spots/tictactoe_game_spot.hpp"
#include "Application/GameCreation/create_game_room_handler.hpp"

#include <memory>

namespace zlink::samples::tictactoe
{

class play_server_host_factory_t
{
  public:
    static zlink::framework::app_t build (const sample_topology_t &topology, bool auto_stop = true)
    {
        auto app = zlink::framework::app_t::create ();
        if (auto_stop) {
            app.add_hosted_service (std::make_unique<stop_after_start_service_t> (app));
        }
        app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
            options.handlers ()
              .add<create_game_room_handler_t> ("play")
              .add<ensure_player_actor_handler_t> ("play");
            options.codecs ().add_json ();
            options.services ().add_singleton<sample_topology_t> (
              std::make_unique<sample_topology_t> (topology));
            options.add_client_server_channel (sample_names_t::play_channel)
              .enable_server (topology.play_endpoint)
              .use_handler_group ("play");
            options.add_route_mesh_channel (sample_names_t::router_channel)
              .bind (topology.play_router_endpoint)
              .set_routing_id (topology.play_rid)
              .connect (topology.play_router_endpoint)
              .enable_spot_route_egress (sample_names_t::game_spot_discovery);
            options.add_spot_mesh (sample_names_t::game_spot_discovery)
              .add_node (sample_names_t::spot_node)
              .enable_router (topology.play_spot_router_endpoint, topology.play_rid)
              .accept_routes_from_channel (
                sample_names_t::router_channel,
                [&] (zlink::framework::accepted_spot_route_channel_builder_t &routes) {
                    routes.connect (topology.play_router_endpoint);
                })
              .add_entry_spot<entry_spot_t> ()
              .add_spot<tictactoe_game_spot_t> (sample_names_t::match_spot)
              .add_actor_factory<player_actor_factory_t> (sample_names_t::actor_type);
        });
        return app;
    }
};

} // namespace zlink::samples::tictactoe
