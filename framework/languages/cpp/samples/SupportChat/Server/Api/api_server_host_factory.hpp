/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Configuration/sample_configuration.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../../Shared/Contracts/messages.hpp"
#include "../host_support.hpp"
#include "../sample_log_dir.hpp"
#include "Handlers/authenticate_user_handler.hpp"
#include "Handlers/open_conversation_handler.hpp"

namespace zlink::samples::supportchat
{

class api_server_host_factory_t
{
  public:
    static zlink::framework::app_t build (const sample_topology_t &topology, bool auto_stop = true)
    {
        auto app = zlink::framework::app_t::create ();
        configure (app, topology, auto_stop);
        return app;
    }

    static zlink::framework::app_t &configure (zlink::framework::app_t &app,
                                               const sample_topology_t &topology,
                                               bool auto_stop = true)
    {
        app.logging ().use_console ().set_level ("info");
        if (auto_stop) {
            app.add_hosted_service (std::make_unique<stop_after_start_service_t> (app));
        }
        app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
            options.configure_dispatch ()
              .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
              .trace_log_file (flow_log_path ("api"))
              .trace_node_id ("supportchat-api");
            options.handlers ()
              .add<authenticate_user_handler_t> ("api")
              .add<open_conversation_handler_t> ("api");
            options.codecs ().add_json ();
            options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint);
            options.add_client_server_channel (sample_names_t::api_channel)
              .enable_server (topology.api_channel_endpoint)
              .use_handler_group ("api");
            options.add_client_server_channel (sample_names_t::support_channel)
              .enable_client ();
        });
        return app;
    }
};

} // namespace zlink::samples::supportchat
