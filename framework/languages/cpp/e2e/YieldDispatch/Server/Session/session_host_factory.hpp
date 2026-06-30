/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Shared/codecs.hpp"
#include "../Shared/env.hpp"
#include "Support/yield_session.hpp"

#include <zlink/framework.hpp>

namespace zlink::framework::e2e::yield_dispatch::server::session {

namespace yd = zlink::framework::e2e::yield_dispatch;

inline zlink::framework::app_t create_session_host ()
{
    const auto log_dir = server::env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    const auto node_rid = server::env_or ("ZLINK_CPP_E2E_NODE_RID", "session-a");
    const auto http_endpoint = server::env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    const auto registry_router = server::env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER");
    const auto control_endpoint = server::env_or ("ZLINK_CPP_E2E_CONTROL_ENDPOINT");
    const auto spot_router_endpoint = server::env_or ("ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT");
    const auto spot_pub_endpoint = server::env_or ("ZLINK_CPP_E2E_SPOT_PUB_ENDPOINT");
    const auto stream_endpoint = server::env_or ("ZLINK_CPP_E2E_STREAM_ENDPOINT");

    auto app = zlink::framework::app_t::create ();
    app.logging ()
      .use_file (log_dir + "/" + node_rid + ".log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([=] (zlink::framework::zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/" + node_rid + "-flow.log")
          .trace_label ("cpp-yd-" + node_rid);
        server::configure_codecs (options.codecs ());
        options.use_discovery ().add_registry_endpoint (registry_router);
        options.add_route_mesh (yd::control_channel)
          .enable_client (control_endpoint)
          .set_routing_id (zlink::routing_id_t::from (node_rid));
        options.add_spot_mesh (yd::spot_channel)
          .use_registry_spot_resolver (yd::control_channel)
          .set_routing_id (zlink::routing_id_t::from (node_rid))
          .enable_router (spot_router_endpoint)
          .enable_pub_sub (spot_pub_endpoint);
        options.add_stream_node (yd::stream_node)
          .bind (stream_endpoint)
          .register_session<yield_session_t> ();
        options.http ().listen (http_endpoint).map_health ("/health");
    });
    return app;
}

} // namespace zlink::framework::e2e::yield_dispatch::server::session
