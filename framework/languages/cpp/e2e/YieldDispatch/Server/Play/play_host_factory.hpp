/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "Handlers/play_actor_handlers.hpp"
#include "Handlers/play_control_handlers.hpp"
#include "Spots/play_spot_runtime.hpp"
#include "Spots/play_spot_types.hpp"
#include "Support/play_support.hpp"
#include "../Shared/codecs.hpp"
#include "../Shared/location_store.hpp"

#include <zlink/framework.hpp>

#include <memory>

namespace zlink::framework::e2e::yield_dispatch::server::play {

namespace yd = zlink::framework::e2e::yield_dispatch;

inline zlink::framework::app_t create_play_host ()
{
    const auto play_options = read_play_options ();

    auto app = zlink::framework::app_t::create ();
    app.logging ()
      .use_file (play_options.log_dir + "/" + play_options.node_rid + ".log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([=] (zlink::framework::zlink_framework_options_t &options) {
        auto evidence =
          std::make_unique<evidence_store_t> (
            play_options.node_rid,
            play_options.log_dir + "/" + play_options.node_rid + ".evidence.log");
        auto *evidence_ptr = evidence.get ();
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (play_options.log_dir + "/" + play_options.node_rid + "-flow.log")
          .trace_label ("cpp-yd-" + play_options.node_rid);
        options.services ()
          .add_singleton<evidence_store_t> (std::move (evidence))
          .add_transient<bind_yield_actors_handler_t, evidence_store_t,
                         zlink::framework::spot_node_manager_t,
                         zlink::framework::session_actor_manager_t> ()
          .add_transient<ensure_spot_handler_t, evidence_store_t,
                         zlink::framework::spot_node_manager_t> ()
          .add_transient<evidence_handler_t, evidence_store_t> ()
          .add_transient<evidence_wait_handler_t, evidence_store_t> ();
        server::configure_codecs (options.codecs ());
        server::add_redis_location_store (options, play_options.redis_endpoint,
                                          play_options.redis_key_prefix);
        options.add_client_server_channel (yd::delay_channel)
          .enable_client (play_options.delay_endpoint)
          .set_routing_id (zlink::routing_id_t::from (play_options.node_rid));
        options.add_route_mesh (yd::control_channel)
          .enable_server (play_options.control_endpoint)
          .enable_client ()
          .set_routing_id (zlink::routing_id_t::from (play_options.node_rid))
          .add_request_handler<bind_yield_actors_handler_t, yd::bind_yield_actors_req_t,
                               yd::bind_yield_actors_res_t> (
            yd::bind_yield_actors_req_t::packet_name, &bind_yield_actors_handler_t::handle)
          .add_request_handler<ensure_spot_handler_t, yd::ensure_spot_req_t,
                               yd::ensure_spot_res_t> (
            yd::ensure_spot_req_t::packet_name, &ensure_spot_handler_t::handle)
          .add_request_handler<evidence_handler_t, yd::yield_evidence_req_t,
                               yd::yield_evidence_res_t> (
            yd::yield_evidence_req_t::packet_name, &evidence_handler_t::handle)
          .add_request_handler<evidence_wait_handler_t, yd::yield_evidence_wait_req_t,
                               yd::yield_evidence_res_t> (
            yd::yield_evidence_wait_req_t::packet_name, &evidence_wait_handler_t::handle);
        options.add_route_mesh (yd::spot_route_channel)
          .enable_server (play_options.spot_route_endpoint)
          .enable_client ()
          .set_routing_id (zlink::routing_id_t::from (play_options.node_rid));
        options.add_spot_mesh (yd::spot_channel)
          .set_routing_id (zlink::routing_id_t::from (play_options.node_rid))
          .enable_router (play_options.spot_router_endpoint)
          .enable_pub_sub (play_options.spot_pub_endpoint)
          .accept_route_mesh (yd::spot_route_channel)
          .add_entry_spot<yield_entry_spot_t> (
            [evidence_ptr] { return std::make_shared<yield_entry_spot_t> (*evidence_ptr); })
          .add_spot<yield_probe_spot_t> (
            yd::probe_spot_name,
            [evidence_ptr] { return std::make_shared<yield_probe_spot_t> (*evidence_ptr); })
          .add_actor_factory<yield_actor_factory_t> (yd::actor_type);
        options.http ().listen (play_options.http_endpoint).map_health ("/health");
    });
    return app;
}

} // namespace zlink::framework::e2e::yield_dispatch::server::play
