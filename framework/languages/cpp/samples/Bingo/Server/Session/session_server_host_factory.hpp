/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/sample.hpp"

namespace zlink::samples::bingo
{

class session_server_host_factory_t
{
  public:
    static zlink::framework::app_t build (const sample_topology_t &topology)
    {
        auto app = zlink::framework::app_t::create ();
        add_sample_auto_stop (app);
        app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
            options.codecs ().add_json ();
            options.discovery ().add (topology.registry_router_endpoint);
            options.add_client_server_channel (sample_names_t::api_channel).enable_client ();
            options.add_client_server_channel (sample_names_t::play_channel).enable_client ();
            options.add_spot_mesh (sample_names_t::room_spot_discovery)
              .node (sample_names_t::session_spot_node)
              .enable_router (topology.session_router_endpoint, topology.session_router_rid)
              .enable_pub_sub (topology.session_spot_endpoint, topology.session_pub_rid);
            options.add_stream_node (sample_names_t::stream_node)
              .bind (topology.stream_endpoint)
              .packet_session ("bingo-session")
              .attach_actor_gateway (sample_names_t::room_spot_node);
        });
        return app;
    }
};

} // namespace zlink::samples::bingo
