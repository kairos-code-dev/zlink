/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "Handlers/session_session_handlers.hpp"
#include "../Shared/Endpoints/evidence_endpoint.hpp"
#include "../Shared/Handlers/channel_control_ping_handler.hpp"
#include "../Shared/Support/codecs.hpp"
#include "../Shared/Support/env.hpp"
#include "../Shared/Support/location_store.hpp"

#include <zlink/framework.hpp>

#include <memory>
#include <string>

inline int run_session_server (int argc, char **argv)
{
    auto app = zlink::framework::app_t::create ();
    const auto log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    const auto node_rid = env_or ("ZLINK_CPP_E2E_NODE_RID", "session-a");
    const auto route_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_ENDPOINT");
    const auto spot_router_endpoint = env_or ("ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT");
    const auto pubsub_endpoint = env_or ("ZLINK_CPP_E2E_PUBSUB_ENDPOINT");
    const auto http_endpoint = env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    const auto redis_endpoint = env_or ("ZLINK_CPP_E2E_REDIS_ENDPOINT");
    const auto redis_key_prefix = env_or ("ZLINK_CPP_E2E_REDIS_KEY_PREFIX");
    const auto stream_endpoint = env_or ("ZLINK_CPP_E2E_STREAM_ENDPOINT");
    const auto tls_stream_endpoint = env_or ("ZLINK_CPP_E2E_TLS_STREAM_ENDPOINT");
    const auto tls_cert_path = env_or ("ZLINK_CPP_E2E_TLS_CERT_PATH");
    const auto tls_key_path = env_or ("ZLINK_CPP_E2E_TLS_KEY_PATH");

    app.logging ()
      .use_file (log_dir + "/" + node_rid + ".log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        auto state = std::make_unique<scenario_state_t> (node_rid);
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/" + node_rid + "-flow.log")
          .trace_label ("cpp-sm-" + node_rid);
        options.services ().add_singleton<scenario_state_t> (std::move (state));
        configure_codecs (options.codecs ());
        add_redis_location_store (options, redis_endpoint, redis_key_prefix);

        options.add_route_mesh_channel (e2e::route_channel)
          .enable_server (route_endpoint)
          .set_routing_id (zlink::routing_id_t::from (node_rid))
          .enable_client ();
        options.add_spot_mesh (e2e::spot_mesh)
          .set_routing_id (zlink::routing_id_t::from (node_rid))
          .enable_router (spot_router_endpoint)
          .enable_pub_sub (pubsub_endpoint);
        options.add_stream_node ("spot-service-stream")
          .bind (stream_endpoint)
          .register_session<stream_session_t> ();
        if (!tls_stream_endpoint.empty ()) {
            options.add_stream_node ("spot-service-tls-stream")
              .bind (tls_stream_endpoint)
              .set_tls_server (tls_cert_path, tls_key_path)
              .register_session<stream_session_t> ();
        }
        options.http ()
          .listen (http_endpoint)
          .map_health ("/health")
          .map_get<evidence_handler_t> ("/evidence")
          .map_post<evidence_wait_handler_t> ("/evidence/wait")
          .map_post<channel_control_ping_route_handler_t> ("/channel/control-ping")
          .map_post<shutdown_handler_t> ("/shutdown")
          .map_post<crash_handler_t> ("/crash");
    });
    return app.run (argc, argv);
}
