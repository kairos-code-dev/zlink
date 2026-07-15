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
        server::add_redis_location_store (framework, options.redis_endpoint,
                                          options.redis_key_prefix);
        framework.add_client_server_channel (profile_channel)
          .enable_server (options.channel_endpoint)
          .set_routing_id (zlink::routing_id_t::from (options.rid))
          .use_handler_group (handler_group);
        framework.add_spot_mesh (spot_node)
          .set_routing_id (zlink::routing_id_t::from (options.rid))
          .enable_router (options.spot_router_endpoint)
          .enable_pub_sub (options.spot_pub_endpoint)
          .add_spot<monitoring_spot_t> (spot_channel);
        if (!options.log_dir.empty ()) {
            framework.configure_dispatch ()
              .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
              .trace_log_file (options.log_dir + "/" + options.rid + "-flow.log")
              .trace_label ("cpp-mon-" + options.rid);
        }
        framework.handlers ().group (handler_group).add<profile_request_handler_t> ();
        auto &monitoring = framework.monitoring ();
        monitoring.add_socket_events (channel_server_source);
        if (options.monitor_profile == "socket-filter") {
            monitoring.add_socket_events (
              profile_channel, {zlink::framework::socket_event_kind_t::connection_ready});
        } else {
            monitoring.add_socket_events (profile_channel);
        }
        monitoring.add_spot_events (spot_node, std::chrono::milliseconds (100));
        monitoring.add_location_events ("location-runtime", std::chrono::milliseconds (100));
        monitoring.on<zlink::framework::socket_event_payload_t> (
          [evidence_ptr] (const zlink::framework::socket_event_payload_t &event) {
              server::record_socket_event (*evidence_ptr, event);
          });
        monitoring.on<zlink::framework::location_event_payload_t> (
          [evidence_ptr] (const zlink::framework::location_event_payload_t &event) {
              server::record_location_event (*evidence_ptr, event);
          });
        monitoring.on<zlink::framework::spot_event_payload_t> (
          [evidence_ptr] (const zlink::framework::spot_event_payload_t &event) {
              server::record_spot_event (*evidence_ptr, event);
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
              .map_post<shutdown_handler_t> ("/shutdown");
        }
    });
    return app.run (argc, argv);
}

} // namespace zlink::framework::e2e::runtime_monitoring::service
