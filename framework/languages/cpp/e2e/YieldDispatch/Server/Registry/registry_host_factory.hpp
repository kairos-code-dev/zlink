/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "Support/registry_support.hpp"

#include <zlink/framework.hpp>

namespace zlink::framework::e2e::yield_dispatch::server::registry {

inline zlink::framework::app_t create_registry_host ()
{
    const auto registry_options = read_registry_options ();

    auto app = zlink::framework::app_t::create ();
    app.logging ()
      .use_file (registry_options.log_dir + "/registry.log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([=] (zlink::framework::zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (registry_options.log_dir + "/registry-flow.log")
          .trace_label ("cpp-yd-registry");
        options.enable_registry (registry_options.pub_endpoint,
                                 registry_options.router_endpoint);
        if (!registry_options.http_endpoint.empty ()) {
            options.http ().listen (registry_options.http_endpoint).map_health ("/health");
        }
    });
    return app;
}

} // namespace zlink::framework::e2e::yield_dispatch::server::registry
