/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/E2E/client_e2e_stream_server.hpp"
#include "../../Shared/sample.hpp"

namespace zlink::samples::bingo
{

class session_server_host_factory_t
{
  public:
    static zlink::framework::app_t build (const sample_topology_t &topology, bool auto_stop = true)
    {
        auto app = zlink::framework::app_t::create ();
        if (auto_stop) {
            app.add_hosted_service (std::make_unique<stop_after_start_service_t> (app));
        } else {
            app.add_hosted_service (std::make_unique<sample_stream_server_service_t> (
              app, topology.stream_endpoint, run_client_e2e_stream_server));
        }
        app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
            options.codecs ().add_json ();
            options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint);
            options.add_client_server_channel (sample_names_t::api_channel).enable_client ();
            options.add_client_server_channel (sample_names_t::play_channel).enable_client ();
            options.add_spot_mesh (sample_names_t::room_spot_discovery)
              .add_node (sample_names_t::session_spot_node)
              .enable_router (topology.session_router_endpoint, topology.session_router_rid)
              .enable_pub_sub (topology.session_spot_endpoint, topology.session_pub_rid);
            options.add_stream_node (sample_names_t::stream_node)
              .bind (topology.stream_endpoint)
              .register_session ("bingo-session")
              .attach_actor_gateway (sample_names_t::room_spot_node);
        });
        return app;
    }
};

} // namespace zlink::samples::bingo
