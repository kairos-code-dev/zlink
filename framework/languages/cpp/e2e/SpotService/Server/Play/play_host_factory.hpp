/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "Endpoints/operational_endpoints.hpp"
#include "Endpoints/spot_failure_endpoints.hpp"
#include "Endpoints/spot_interaction_endpoints.hpp"
#include "Endpoints/spot_lifecycle_endpoints.hpp"
#include "Handlers/play_actor_handlers.hpp"
#include "Handlers/play_control_handlers.hpp"
#include "Handlers/play_session_handlers.hpp"
#include "Handlers/play_spot_route_handlers.hpp"
#include "Spots/play_actor_model.hpp"
#include "../Shared/Handlers/channel_control_ping_handler.hpp"
#include "../Shared/Support/codecs.hpp"
#include "../Shared/Support/env.hpp"
#include "../Shared/Support/location_store.hpp"

#include <zlink/framework.hpp>

#include <memory>
#include <string>

inline int run_play_server (int argc, char **argv)
{
    auto app = zlink::framework::app_t::create ();
    const auto log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    const auto node_rid = env_or ("ZLINK_CPP_E2E_NODE_RID", "play-a");
    const auto route_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_ENDPOINT");
    const auto spot_router_endpoint = env_or ("ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT");
    const auto pubsub_endpoint = env_or ("ZLINK_CPP_E2E_PUBSUB_ENDPOINT");
    const auto api_peer_endpoint = env_or ("ZLINK_CPP_E2E_API_PEER_ENDPOINT");
    const auto api_endpoint = env_or ("ZLINK_CPP_E2E_API_ENDPOINT");
    const auto publisher_endpoint = env_or ("ZLINK_CPP_E2E_PUBLISHER_ENDPOINT");
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
          .add_transient<ensure_actor_handler_t, scenario_state_t,
                         zlink::framework::spot_node_manager_t,
                         zlink::framework::session_actor_manager_t,
                         zlink::framework::route_client_t,
                         zlink::framework::actor_gateway_t> ()
          .add_transient<spot_lifecycle_handler_t, scenario_state_t,
                         zlink::framework::spot_node_manager_t> ()
          .add_transient<join_spot_handler_t, scenario_state_t,
                         zlink::framework::session_actor_manager_t> ()
          .add_transient<complex_actor_handler_t, scenario_state_t,
                         zlink::framework::session_actor_manager_t> ()
          .add_transient<missing_actor_handler_t, scenario_state_t,
                         zlink::framework::session_actor_manager_t> ()
          .add_transient<push_bound_session_handler_t,
                         zlink::framework::session_actor_manager_t> ()
          .add_transient<remote_actor_flow_handler_t, scenario_state_t,
                         zlink::framework::session_actor_manager_t> ()
          .add_transient<remote_actor_request_handler_t, scenario_state_t,
                         zlink::framework::route_client_t,
                         zlink::framework::session_actor_manager_t> ()
          .add_transient<worker_spot_handler_t, zlink::framework::session_actor_manager_t> ()
          .add_transient<create_spot_handler_t, scenario_state_t,
                         zlink::framework::spot_node_manager_t> ()
          .add_transient<create_alternate_spot_handler_t, scenario_state_t,
                         zlink::framework::spot_node_manager_t> ()
          .add_transient<spot_state_command_route_handler_t,
                         zlink::framework::route_client_t> ()
          .add_transient<spot_publish_route_handler_t,
                         zlink::framework::spot_publisher_client_t> ()
          .add_transient<spot_publish_wait_handler_t, scenario_state_t> ()
          .add_transient<spot_worker_start_route_handler_t,
                         zlink::framework::route_client_t> ()
          .add_transient<spot_worker_complete_handler_t, scenario_state_t> ()
          .add_transient<spot_stage_probe_route_handler_t,
                         zlink::framework::route_client_t> ()
          .add_transient<spot_stage_timer_route_handler_t,
                         zlink::framework::route_client_t,
                         scenario_state_t> ()
          .add_transient<spot_idle_close_route_handler_t, scenario_state_t,
                         zlink::framework::spot_node_manager_t> ()
          .add_transient<spot_overrun_start_route_handler_t, scenario_state_t,
                         zlink::framework::spot_node_manager_t> ()
          .add_transient<spot_slow_route_handler_t, zlink::framework::route_client_t> ()
          .add_transient<spot_missing_handler_request_handler_t,
                         zlink::framework::route_client_t,
                         scenario_state_t> ()
          .add_transient<spot_missing_handler_command_handler_t,
                         zlink::framework::route_client_t,
                         scenario_state_t> ()
          .add_transient<spot_missing_target_request_handler_t,
                         zlink::framework::route_client_t> ()
          .add_transient<spot_missing_route_handler_t, zlink::framework::route_client_t> ()
          .add_transient<spot_outbound_route_handler_t, zlink::framework::route_client_t> ()
          .add_transient<spot_outbound_negative_route_handler_t,
                         zlink::framework::route_client_t> ()
          .add_transient<spot_to_spot_route_handler_t, zlink::framework::route_client_t> ()
          .add_transient<spot_to_spot_timeout_route_handler_t,
                         zlink::framework::route_client_t> ()
          .add_transient<spot_to_spot_negative_route_handler_t,
                         zlink::framework::route_client_t> ()
          .add_transient<lifecycle_spot_handler_t, scenario_state_t,
                         zlink::framework::spot_node_manager_t> ()
          .add_transient<close_spot_handler_t, scenario_state_t,
                         zlink::framework::spot_node_manager_t> ()
          .add_transient<type_mismatch_spot_handler_t, scenario_state_t,
                         zlink::framework::spot_node_manager_t,
                         zlink::framework::session_actor_manager_t> ();
        configure_codecs (options.codecs ());
        add_redis_location_store (options, redis_endpoint, redis_key_prefix);

        options.add_route_mesh_channel (e2e::route_channel)
          .enable_server (route_endpoint)
          .set_routing_id (zlink::routing_id_t::from (node_rid))
          .enable_client ()
          .add_request_handler<ensure_actor_handler_t, e2e::ensure_actor_req_t,
                               e2e::ensure_actor_res_t> (
            "EnsureActor", &ensure_actor_handler_t::handle)
          .add_request_handler<channel_echo_handler_t, e2e::channel_echo_req_t,
                               e2e::channel_echo_res_t> (
            "ChannelEchoReq", &channel_echo_handler_t::route_handle)
          .add_request_handler<spot_lifecycle_handler_t, e2e::lifecycle_req_t,
                               e2e::lifecycle_res_t> (
            "LifecycleReq", &spot_lifecycle_handler_t::handle);
        if (!api_endpoint.empty () || !api_peer_endpoint.empty ()) {
            auto api = options.add_client_server_channel (e2e::api_channel);
            if (!api_endpoint.empty ()) {
                api.enable_server (api_endpoint).use_handler_group (e2e::handler_group);
            }
            if (!api_peer_endpoint.empty ()) {
                api.enable_client (api_peer_endpoint);
            }
        }
        if (!publisher_endpoint.empty ()) {
            options.add_fanout_channel (e2e::publisher_channel)
              .enable_publisher (publisher_endpoint);
        }
        options.add_spot_mesh (e2e::spot_mesh)
          .set_routing_id (zlink::routing_id_t::from (node_rid))
          .enable_router (spot_router_endpoint)
          .enable_pub_sub (pubsub_endpoint)
          .add_entry_spot<entry_spot_t> (
            [state_ptr] { return std::make_shared<entry_spot_t> (*state_ptr); })
          .add_spot<user_spot_t> (
            e2e::user_spot, [state_ptr] { return std::make_shared<user_spot_t> (*state_ptr); })
          .add_spot<alternate_user_spot_t> (e2e::alternate_spot)
          .add_actor_factory<scenario_actor_factory_t> (e2e::actor_type);
        auto &http = options.http ().listen (http_endpoint);
        map_operational_endpoints (http);
        map_spot_lifecycle_endpoints (http);
        map_spot_interaction_endpoints (http);
        map_spot_failure_endpoints (http);
        options.handlers ()
          .group (e2e::handler_group)
          .add<channel_echo_handler_t> ()
          .add_send<channel_command_handler_t> ()
          .add<channel_slow_handler_t> ();
    });
    return app.run (argc, argv);
}
