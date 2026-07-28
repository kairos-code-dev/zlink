/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "service_options.hpp"

#include "../Handlers/service_event_recorders.hpp"
#include "../Handlers/service_handlers.hpp"
#include "../../Shared/evidence_store.hpp"
#include "../../Shared/location_store.hpp"
#include "../../../Shared/runtime_monitoring_contracts.hpp"

#include <zlink/framework.hpp>

#include <memory>
#include <stdexcept>
#include <string>

namespace zlink::framework::e2e::runtime_monitoring::service
{

inline int run_service_host (int argc, char **argv)
{
    auto app = zlink::framework::app_t::create ();
    const auto options = read_service_options (app, argc, argv);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &framework) {
        auto evidence =
          std::make_unique<server::evidence_store_t> (options.rid, options.evidence_file);
        auto *evidence_ptr = evidence.get ();
        framework.services ().add_singleton<server::evidence_store_t> (std::move (evidence));
        framework.services ().add_singleton<runtime_observation_store_t> ();
        auto gate = std::make_unique<application_gate_t> ();
        auto *gate_ptr = gate.get ();
        framework.services ().add_singleton<application_gate_t> (
          std::move (gate));
        framework.services ()
          .add_transient<mesh_profile_request_dispatch_handler_t,
                         server::evidence_store_t> ();
        framework.services ()
          .add_transient<mesh_application_gate_dispatch_handler_t,
                         application_gate_t,
                         server::evidence_store_t> ();
        server::add_redis_location_store (framework, options.redis_endpoint,
                                          options.redis_key_prefix);
        framework.add_client_server_channel (profile_channel)
          .enable_server (options.channel_endpoint)
          .set_routing_id (zlink::routing_id_t::from (options.rid))
          .use_handler_group (handler_group);
        if (!options.log_dir.empty ()) {
            framework.configure_dispatch ()
              .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
              .trace_log_file (options.log_dir + "/" + options.rid + "-flow.log")
              .trace_label ("cpp-mon-" + options.rid);
        }
        framework.handlers ().group (handler_group).add<profile_request_handler_t> ();
        auto mesh = framework.add_route_mesh (route_mesh_name);
        mesh.listen (options.mesh_endpoint)
          .set_routing_id (zlink::routing_id_t::from (options.rid))
          .channel_name (route_mesh_channel)
          .use_handler_group (handler_group);
        mesh.channel_name (route_mesh_channel)
          .add_request_handler<mesh_profile_request_dispatch_handler_t,
                               profile_req_t,
                               profile_res_t> ()
          .add_request_handler<mesh_application_gate_dispatch_handler_t,
                               application_gate_req_t,
                               application_gate_res_t> ();
        mesh.configure_router_socket ().send_high_water_mark = 1;
        mesh.configure_router_socket ().send_timeout =
          std::chrono::milliseconds (250);
        mesh.configure_router_socket ().mailbox_message_budget = 1;
        mesh.configure_router_socket ().mailbox_byte_budget = 2 * 1024 * 1024;
        mesh.add_spot_factory<monitoring_spot_t> (
          spot_channel,
          [] (zlink::framework::spot_context_t) {
              return std::make_shared<monitoring_spot_t> ();
          },
          [] (auto &factory) {
              factory.disable_relocation ();
          });
        mesh.add_spot_factory<monitoring_subject_spot_t> (
          monitoring_subject_spot,
          [] (zlink::framework::spot_context_t) {
              return std::make_shared<monitoring_subject_spot_t> ();
          },
          [] (auto &factory) {
              factory.disable_relocation ();
          });
        for (const auto &endpoint : options.mesh_peer_endpoints)
            mesh.peer_connections ().connect (endpoint);
        auto &monitoring = framework.monitoring ();
        monitoring.add_socket_events (channel_server_source);
        if (options.monitor_profile == "socket-filter") {
            monitoring.add_socket_events (
              profile_channel, {zlink::framework::socket_event_kind_t::connection_ready});
        } else {
            monitoring.add_socket_events (profile_channel);
        }
        monitoring.add_location_events ("location-runtime", std::chrono::milliseconds (100));
        monitoring.on<zlink::framework::socket_event_payload_t> (
          [evidence_ptr] (const zlink::framework::socket_event_payload_t &event) {
              server::record_socket_event (*evidence_ptr, event);
          });
        monitoring.on<zlink::framework::location_event_payload_t> (
          [evidence_ptr] (const zlink::framework::location_event_payload_t &event) {
              server::record_location_event (*evidence_ptr, event);
          });
        if (options.monitor_profile == "throwing") {
            monitoring.on<zlink::framework::socket_event_payload_t> (
              [evidence_ptr] (const zlink::framework::socket_event_payload_t &event) {
                  record_throwing_socket_event (*evidence_ptr, event);
              });
        }
        if (!options.http_endpoint.empty ()) {
            framework.http ()
              .listen (options.http_endpoint)
              .map_health ("/health")
              .map_get<server::evidence_handler_t> ("/evidence")
              .map_post<server::evidence_wait_handler_t> ("/evidence/wait")
              .map_post<server_weight_handler_t> ("/admin/server-weight")
              .map_post<create_spot_handler_t> ("/spot/create")
              .map_post<create_subject_handler_t> (
                "/admin/subject/create")
              .map_post<close_subject_handler_t> (
                "/admin/subject/close")
              .map_post<publish_probe_handler_t> (
                "/runtime/publish")
              .map_post<runtime_observe_handler_t> ("/runtime/observe")
              .map_post<runtime_observe_isolation_handler_t> (
                "/runtime/observe-isolation")
              .map_get<runtime_snapshot_handler_t> ("/runtime/snapshot")
              .map_post<application_gate_arm_handler_t> (
                "/admin/application-gate/arm")
              .map_post<application_gate_wait_handler_t> (
                "/admin/application-gate/wait")
              .map_post<application_gate_release_handler_t> (
                "/admin/application-gate/release")
              .map_post<mesh_profile_request_handler_t> (
                "/mesh/profile/request")
              .map_post<mesh_application_gate_request_handler_t> (
                "/mesh/application-gate/request")
              .map_post<mesh_weight_handler_t> ("/admin/mesh-weight")
              .map_get<runtime_validation_handler_t> ("/runtime/validation")
              .map_post<shutdown_handler_t> ("/shutdown");
        }
    });
    return app.run (argc, argv);
}

} // namespace zlink::framework::e2e::runtime_monitoring::service
