/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "Handlers/play_actor_handlers.hpp"
#include "Handlers/play_control_handlers.hpp"
#include "Spots/play_spot_runtime.hpp"
#include "Spots/play_spot_types.hpp"
#include "Support/play_support.hpp"
#include "../Shared/codecs.hpp"
#include "../Shared/env.hpp"

#include <zlink/framework.hpp>

#include <memory>

namespace zlink::framework::e2e::yield_dispatch::server::play {

namespace yd = zlink::framework::e2e::yield_dispatch;

inline zlink::framework::app_t create_play_host ()
{
    const auto log_dir = server::env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    const auto node_rid = server::env_or ("ZLINK_CPP_E2E_NODE_RID", "play-a");
    const auto http_endpoint = server::env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    const auto registry_router = server::env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER");
    const auto control_endpoint = server::env_or ("ZLINK_CPP_E2E_CONTROL_ENDPOINT");
    const auto delay_endpoint = server::env_or ("ZLINK_CPP_E2E_DELAY_ENDPOINT");
    const auto spot_router_endpoint = server::env_or ("ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT");
    const auto spot_pub_endpoint = server::env_or ("ZLINK_CPP_E2E_SPOT_PUB_ENDPOINT");

    auto app = zlink::framework::app_t::create ();
    app.logging ()
      .use_file (log_dir + "/" + node_rid + ".log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([=] (zlink::framework::zlink_framework_options_t &options) {
        auto evidence = std::make_unique<evidence_store_t> (node_rid);
        auto *evidence_ptr = evidence.get ();
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/" + node_rid + "-flow.log")
          .trace_label ("cpp-yd-" + node_rid);
        options.services ()
          .add_singleton<evidence_store_t> (std::move (evidence))
          .add_transient<bind_yield_actors_handler_t, evidence_store_t,
                         zlink::framework::spot_node_manager_t> ()
          .add_transient<ensure_spot_handler_t, evidence_store_t,
                         zlink::framework::spot_node_manager_t> ()
          .add_transient<evidence_handler_t, evidence_store_t> ()
          .add_transient<evidence_wait_handler_t, evidence_store_t> ();
        server::configure_codecs (options.codecs ());
        options.use_discovery ().add_registry_endpoint (registry_router);
        options.add_client_server_channel (yd::delay_channel)
          .enable_client (delay_endpoint)
          .set_routing_id (zlink::routing_id_t::from (node_rid));
        options.add_route_mesh (yd::control_channel)
          .enable_server (control_endpoint)
          .enable_client ()
          .set_routing_id (zlink::routing_id_t::from (node_rid))
          .add_request_handler<bind_yield_actors_handler_t, yd::bind_yield_actors_req_t,
                               yd::bind_yield_actors_reply_t> (
            yd::bind_yield_actors_req_t::packet_name, &bind_yield_actors_handler_t::handle)
          .add_request_handler<ensure_spot_handler_t, yd::ensure_spot_req_t,
                               yd::ensure_spot_reply_t> (
            yd::ensure_spot_req_t::packet_name, &ensure_spot_handler_t::handle)
          .add_request_handler<evidence_handler_t, yd::yield_evidence_req_t,
                               yd::yield_evidence_reply_t> (
            yd::yield_evidence_req_t::packet_name, &evidence_handler_t::handle)
          .add_request_handler<evidence_wait_handler_t, yd::yield_evidence_wait_req_t,
                               yd::yield_evidence_reply_t> (
            yd::yield_evidence_wait_req_t::packet_name, &evidence_wait_handler_t::handle);
        options.add_spot_mesh (yd::spot_channel)
          .use_registry_spot_resolver (yd::control_channel)
          .set_routing_id (zlink::routing_id_t::from (node_rid))
          .enable_router (spot_router_endpoint)
          .enable_pub_sub (spot_pub_endpoint)
          .add_entry_spot<yield_entry_spot_t> (
            [evidence_ptr] { return std::make_shared<yield_entry_spot_t> (*evidence_ptr); })
          .add_spot<yield_probe_spot_t> (
            yd::probe_spot_name,
            [evidence_ptr] { return std::make_shared<yield_probe_spot_t> (*evidence_ptr); })
          .add_actor_factory<yield_actor_factory_t> (yd::actor_type);
        options.http ().listen (http_endpoint).map_health ("/health");
    });
    return app;
}

} // namespace zlink::framework::e2e::yield_dispatch::server::play
