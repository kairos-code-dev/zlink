/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Configuration/sample_configuration.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../../Shared/Contracts/codecs.hpp"
#include "../../Shared/Contracts/messages.hpp"
#include "../host_support.hpp"
#include "../sample_log_dir.hpp"
#include "Sessions/support_chat_session.hpp"

#include <memory>

namespace zlink::samples::supportchat
{

using namespace framework;

class session_server_host_factory_t
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
              .trace_log_file (flow_log_path ("session"))
              .trace_label ("supportchat-session");
            options.codecs ().use (support_chat_json_codec ());
            options.services ().add_singleton<sample_topology_t> (
              std::make_unique<sample_topology_t> (topology));
            options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint);
            options.add_client_server_channel (sample_names_t::api_channel).enable_client ();
            options.add_client_server_channel (sample_names_t::support_channel).enable_client ();
            options.add_spot_mesh (sample_names_t::support_spot_discovery)
              .use_registry_spot_resolver (sample_names_t::actor_session_route_channel)
              .set_routing_id (topology.session_router_rid)
              .enable_router (topology.session_router_endpoint)
              .enable_pub_sub (topology.session_spot_endpoint);
            options.add_route_mesh (sample_names_t::actor_session_route_channel)
              .enable_server (topology.session_actor_route_endpoint)
              .set_routing_id (topology.session_router_rid)
              .enable_client ();
            options.add_stream_node (sample_names_t::stream_node)
              .bind (topology.stream_endpoint)
              .register_session<support_chat_session_t> ();
        });
        return app;
    }
};

} // namespace zlink::samples::supportchat
