/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Shared/env.hpp"

#include <zlink/framework.hpp>

namespace zlink::framework::e2e::yield_dispatch::server::registry {

inline zlink::framework::app_t create_registry_host ()
{
    const auto log_dir = server::env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    const auto http_endpoint = server::env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    const auto pub_endpoint = server::env_or ("ZLINK_CPP_E2E_REGISTRY_PUB");
    const auto router_endpoint = server::env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER");

    auto app = zlink::framework::app_t::create ();
    app.logging ()
      .use_file (log_dir + "/registry.log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([=] (zlink::framework::zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/registry-flow.log")
          .trace_label ("cpp-yd-registry");
        options.enable_registry (pub_endpoint, router_endpoint);
        if (!http_endpoint.empty ()) {
            options.http ().listen (http_endpoint).map_health ("/health");
        }
    });
    return app;
}

} // namespace zlink::framework::e2e::yield_dispatch::server::registry
