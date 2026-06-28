/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Configuration/sample_configuration.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../../Shared/Contracts/codecs.hpp"
#include "../../Shared/Contracts/messages.hpp"
#include "../host_support.hpp"
#include "../sample_log_dir.hpp"
#include "Handlers/authenticate_user_handler.hpp"
#include "Handlers/open_conversation_handler.hpp"

namespace zlink::samples::supportchat
{

using namespace framework;

class api_server_host_factory_t
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
        app.logging ().use_console ().set_level ("info");
        if (auto_stop) {
            app.add_hosted_service (std::make_unique<stop_after_start_service_t> (app));
        }
        app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
            options.configure_dispatch ()
              .message_flow (message_flow_log_mode_t::key_transitions)
              .trace_log_file (flow_log_path ("api"))
              .trace_label ("supportchat-api");
            options.codecs ().use (support_chat_json_codec ());
            options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint);
            options.add_client_server_channel (sample_names_t::api_channel)
              .enable_server (topology.api_channel_endpoint)
              .use_handler_group ("api");
            options.add_client_server_channel (sample_names_t::support_channel).enable_client ();
            options.handlers ()
              .group ("api")
              .add<authenticate_user_handler_t> ()
              .add<open_conversation_handler_t> ();
        });
        return app;
    }
};

} // namespace zlink::samples::supportchat
