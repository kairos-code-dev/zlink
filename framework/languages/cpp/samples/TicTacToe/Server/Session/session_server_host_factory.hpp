/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/sample.hpp"

namespace zlink::samples::tictactoe
{

class session_server_host_factory_t
{
  public:
    static zlink::framework::app_t build (const sample_topology_t &topology)
    {
        auto app = zlink::framework::app_t::create ();
        app.add_hosted_service (std::make_unique<stop_after_start_service_t> (app));
        app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
            options.codecs ().add_json ();
            options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint);
            options.add_client_server_channel (sample_names_t::api_channel).enable_client ();
            options.add_client_server_channel (sample_names_t::play_channel).enable_client ();
            options.use_registry_spot_remote_addresses (sample_names_t::router_channel);
            options.add_route_mesh_channel (sample_names_t::router_channel)
              .bind (topology.session_spot_endpoint)
              .set_routing_id (topology.session_rid)
              .connect (topology.play_router_endpoint);
            options.add_spot_mesh (sample_names_t::game_spot_discovery)
              .add_node (sample_names_t::session_spot_node)
              .enable_router (topology.session_router_endpoint, topology.session_rid)
              .accept_routes_from_channel (sample_names_t::router_channel);
            options.add_stream_node (sample_names_t::stream_name)
              .bind (topology.stream_endpoint)
              .register_session ("client-session")
              .attach_actor_gateway (sample_names_t::session_spot_node);
        });
        return app;
    }
};

} // namespace zlink::samples::tictactoe
