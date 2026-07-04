/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "Handlers/multi_node_handlers.hpp"
#include "../Shared/Endpoints/evidence_endpoint.hpp"
#include "../Shared/Support/codecs.hpp"
#include "../Shared/Support/env.hpp"
#include "../Shared/Support/location_store.hpp"

#include <zlink/framework.hpp>

#include <memory>
#include <string>

inline int run_multi_node_server (int argc, char **argv)
{
    auto app = zlink::framework::app_t::create ();
    const auto log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    const auto node_rid = env_or ("ZLINK_CPP_E2E_NODE_RID", multi_node_a_name);
    const auto route_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_ENDPOINT");
    const auto peer_route_endpoint = env_or ("ZLINK_CPP_E2E_PEER_ROUTE_ENDPOINT");
    const auto spot_router_endpoint = env_or ("ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT");
    const auto pubsub_endpoint = env_or ("ZLINK_CPP_E2E_PUBSUB_ENDPOINT");
    const auto http_endpoint = env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    const auto redis_endpoint = env_or ("ZLINK_CPP_E2E_REDIS_ENDPOINT");
    const auto redis_key_prefix = env_or ("ZLINK_CPP_E2E_REDIS_KEY_PREFIX");

    app.logging ()
      .use_file (log_dir + "/" + node_rid + ".log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        auto state = std::make_unique<scenario_state_t> (node_rid);
        auto *state_ptr = state.get ();
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/" + node_rid + "-flow.log")
          .trace_label ("cpp-sm-" + node_rid);
        options.services ()
          .add_singleton<scenario_state_t> (std::move (state))
          .add_transient<multi_node_create_local_handler_t, scenario_state_t,
                         zlink::framework::spot_node_manager_t,
                         zlink::framework::route_client_t> ()
          .add_transient<multi_node_state_route_handler_t, scenario_state_t,
                         zlink::framework::route_client_t> ();
        configure_codecs (options.codecs ());
        add_redis_location_store (options, redis_endpoint, redis_key_prefix);

        auto route = options.add_route_mesh_channel (multi_node_route_channel_for (node_rid))
                       .enable_server (route_endpoint)
                       .set_routing_id (zlink::routing_id_t::from (node_rid))
                       .enable_client ()
                       .add_request_handler<multi_node_create_local_handler_t,
                                            e2e::multi_node_create_spot_req_t,
                         e2e::multi_node_create_spot_res_t> (
                         "MultiNodeCreateSpotReq",
                         &multi_node_create_local_handler_t::handle_route);
        if (!route_endpoint.empty ()) {
            route.enable_client (route_endpoint);
        }
        if (!peer_route_endpoint.empty ()) {
            route.enable_client (peer_route_endpoint);
        }
        auto spot = options.add_spot_mesh (e2e::spot_mesh)
                      .set_routing_id (zlink::routing_id_t::from (node_rid))
                      .enable_router (spot_router_endpoint)
                      .enable_pub_sub (pubsub_endpoint);
        if (node_rid == multi_node_a_name) {
            spot.add_spot<multi_node_spot_a_t> (
              e2e::multi_spot_a,
              [state_ptr] { return std::make_shared<multi_node_spot_a_t> (*state_ptr); });
        } else {
            spot.add_spot<multi_node_spot_b_t> (
              e2e::multi_spot_b,
              [state_ptr] { return std::make_shared<multi_node_spot_b_t> (*state_ptr); });
        }
        options.http ()
          .listen (http_endpoint)
          .map_health ("/health")
          .map_get<evidence_handler_t> ("/evidence")
          .map_post<evidence_wait_handler_t> ("/evidence/wait")
          .map_post<multi_node_create_local_handler_t> ("/spot/create-local")
          .map_post<multi_node_state_route_handler_t> ("/spot/state/request")
          .map_post<shutdown_handler_t> ("/shutdown");
    });
    return app.run (argc, argv);
}
