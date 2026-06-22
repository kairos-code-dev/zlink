/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Configuration/sample_configuration.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../host_support.hpp"
#include "../sample_log_dir.hpp"

namespace zlink::samples::supportchat
{

class registry_host_factory_t
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
        if (auto_stop) {
            app.add_hosted_service (std::make_unique<stop_after_start_service_t> (app));
        }
        app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
            options.configure_dispatch ()
              .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
              .trace_log_file (flow_log_path ("registry"))
              .trace_node_id ("supportchat-registry");
            options.enable_registry (topology.registry_pub_endpoint,
                                     topology.registry_router_endpoint);
        });
        return app;
    }
};

} // namespace zlink::samples::supportchat
