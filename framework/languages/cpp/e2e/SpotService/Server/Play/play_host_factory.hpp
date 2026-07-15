/* SPDX-License-Identifier: FSL-1.1-ALv2 */
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
#include "../Shared/Support/configuration.hpp"
#include "../Shared/Support/location_store.hpp"

#include <zlink/framework.hpp>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

inline std::vector<std::string> split_play_endpoints (const std::string &text)
{
    std::vector<std::string> endpoints;
    std::stringstream input (text);
    std::string endpoint;
    while (std::getline (input, endpoint, ',')) {
        if (!endpoint.empty ()) {
            endpoints.push_back (endpoint);
        }
    }
    return endpoints;
}

struct play_options_t
{
    std::string log_dir;
    std::string node_rid;
    std::string route_endpoint;
    std::string spot_router_endpoint;
    std::string pubsub_endpoint;
    std::vector<std::string> peer_pubsub_endpoints;
    std::string api_peer_endpoint;
    std::string api_endpoint;
    std::string publisher_endpoint;
    std::string http_endpoint;
    std::string play_a_http_endpoint;
    std::string play_b_http_endpoint;
    std::string redis_endpoint;
    std::string redis_key_prefix;

    static play_options_t bind (const zlink::framework::configuration_section_t &section)
    {
        return {.log_dir = section.require ("logDir"),
                .node_rid = section.get ("nodeRid").value_or ("play-a"),
                .route_endpoint = section.require ("routeEndpoint"),
                .spot_router_endpoint = section.require ("spotRouterEndpoint"),
                .pubsub_endpoint = section.require ("pubsubEndpoint"),
                .peer_pubsub_endpoints = split_play_endpoints (
                  section.get ("peerPubsubEndpoints").value_or ("")),
                .api_peer_endpoint = section.get ("apiPeerEndpoint").value_or (""),
                .api_endpoint = section.get ("apiEndpoint").value_or (""),
                .publisher_endpoint = section.get ("publisherEndpoint").value_or (""),
                .http_endpoint = section.require ("httpEndpoint"),
                .play_a_http_endpoint = section.require ("playHttpEndpoints.playA"),
                .play_b_http_endpoint = section.require ("playHttpEndpoints.playB"),
                .redis_endpoint = section.require ("redis.endpoint"),
                .redis_key_prefix = section.require ("redis.keyPrefix")};
    }
};

inline int run_play_server (int argc, char **argv)
{
    auto app = zlink::framework::app_t::create ();
    load_spot_service_config (app, argc, argv, "play");
    const auto config = app.config ().bind_required<play_options_t> ("e2e");
    const auto &log_dir = config.log_dir;
    const auto &node_rid = config.node_rid;
    const auto &route_endpoint = config.route_endpoint;
    const auto &spot_router_endpoint = config.spot_router_endpoint;
    const auto &pubsub_endpoint = config.pubsub_endpoint;
    const auto &api_peer_endpoint = config.api_peer_endpoint;
    const auto &api_endpoint = config.api_endpoint;
    const auto &publisher_endpoint = config.publisher_endpoint;
    const auto &http_endpoint = config.http_endpoint;
    const auto &redis_endpoint = config.redis_endpoint;
    const auto &redis_key_prefix = config.redis_key_prefix;

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
          .add_singleton<play_node_http_endpoints_t> (
            std::make_unique<play_node_http_endpoints_t> (
              play_node_http_endpoints_t{config.play_a_http_endpoint,
                                         config.play_b_http_endpoint}))
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
                         zlink::framework::session_actor_manager_t,
                         play_node_http_endpoints_t> ()
          .add_transient<remote_actor_request_handler_t, scenario_state_t,
                         zlink::framework::route_client_t,
                         zlink::framework::session_actor_manager_t,
                         play_node_http_endpoints_t> ()
          .add_transient<worker_spot_handler_t, zlink::framework::session_actor_manager_t> ()
          .add_transient<create_spot_handler_t, scenario_state_t,
                         zlink::framework::spot_node_manager_t> ()
          .add_transient<create_alternate_spot_handler_t, scenario_state_t,
                         zlink::framework::spot_node_manager_t> ()
          .add_transient<spot_state_command_route_handler_t,
                         zlink::framework::route_client_t,
                         zlink::framework::spot_handle_resolver_t> ()
          .add_transient<spot_publish_route_handler_t,
                         zlink::framework::spot_publisher_client_t> ()
          .add_transient<spot_publish_wait_handler_t, scenario_state_t> ()
          .add_transient<spot_worker_start_route_handler_t,
                         zlink::framework::route_client_t,
                         zlink::framework::spot_handle_resolver_t> ()
          .add_transient<spot_worker_complete_handler_t, scenario_state_t> ()
          .add_transient<spot_stage_probe_route_handler_t,
                         zlink::framework::route_client_t,
                         zlink::framework::spot_handle_resolver_t> ()
          .add_transient<spot_stage_timer_route_handler_t,
                         zlink::framework::route_client_t,
                         scenario_state_t,
                         zlink::framework::spot_handle_resolver_t> ()
          .add_transient<spot_idle_close_route_handler_t, scenario_state_t,
                         zlink::framework::spot_node_manager_t> ()
          .add_transient<spot_overrun_start_route_handler_t, scenario_state_t,
                         zlink::framework::spot_node_manager_t> ()
          .add_transient<spot_slow_route_handler_t, zlink::framework::route_client_t,
                         zlink::framework::spot_handle_resolver_t> ()
          .add_transient<spot_missing_handler_request_handler_t,
                         zlink::framework::route_client_t,
                         scenario_state_t,
                         zlink::framework::spot_handle_resolver_t> ()
          .add_transient<spot_missing_handler_command_handler_t,
                         zlink::framework::route_client_t,
                         scenario_state_t,
                         zlink::framework::spot_handle_resolver_t> ()
          .add_transient<spot_missing_target_request_handler_t,
                         zlink::framework::route_client_t,
                         zlink::framework::spot_handle_resolver_t> ()
          .add_transient<spot_missing_route_handler_t, zlink::framework::route_client_t,
                         zlink::framework::spot_handle_resolver_t> ()
          .add_transient<spot_outbound_route_handler_t, zlink::framework::route_client_t,
                         zlink::framework::spot_handle_resolver_t> ()
          .add_transient<spot_outbound_negative_route_handler_t,
                         zlink::framework::route_client_t,
                         zlink::framework::spot_handle_resolver_t> ()
          .add_transient<spot_to_spot_route_handler_t, zlink::framework::route_client_t,
                         zlink::framework::spot_handle_resolver_t> ()
          .add_transient<spot_to_spot_timeout_route_handler_t,
                         zlink::framework::route_client_t,
                         zlink::framework::spot_handle_resolver_t> ()
          .add_transient<spot_to_spot_negative_route_handler_t,
                         zlink::framework::route_client_t,
                         zlink::framework::spot_handle_resolver_t> ()
          .add_transient<lifecycle_spot_handler_t, scenario_state_t,
                         zlink::framework::spot_node_manager_t> ()
          .add_transient<close_spot_handler_t, scenario_state_t,
                         zlink::framework::spot_node_manager_t> ()
          .add_transient<type_mismatch_spot_handler_t, scenario_state_t,
                         zlink::framework::spot_node_manager_t,
                         zlink::framework::session_actor_manager_t> ();
        configure_codecs (options.codecs ());
        add_redis_location_store (options, redis_endpoint, redis_key_prefix);

        options.add_route_mesh (e2e::route_channel)
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
        auto spot = options.add_spot_mesh (e2e::spot_mesh)
                      .set_routing_id (zlink::routing_id_t::from (node_rid))
                      .enable_router (spot_router_endpoint)
                      .enable_pub_sub (pubsub_endpoint)
                      .add_entry_spot<entry_spot_t> (
                        [state_ptr] { return std::make_shared<entry_spot_t> (*state_ptr); })
                      .add_spot<user_spot_t> (
                        e2e::user_spot,
                        [state_ptr] { return std::make_shared<user_spot_t> (*state_ptr); })
                      .add_spot<alternate_user_spot_t> (e2e::alternate_spot)
                      .add_actor_factory<scenario_actor_factory_t> (e2e::actor_type);
        for (const auto &endpoint : config.peer_pubsub_endpoints) {
            spot.connect_peer_pub (endpoint);
        }
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
