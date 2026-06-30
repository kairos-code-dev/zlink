/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "Configuration/registry_options.hpp"
#include "Endpoints/registry_endpoints.hpp"
#include "Handlers/registry_handlers.hpp"
#include "Infrastructure/evidence_store.hpp"
#include "Infrastructure/fault_state.hpp"

#include <zlink/framework.hpp>

#include <memory>

namespace zlink::framework::e2e::registry_messaging::registry
{

inline void configure_registry_host (zlink::framework::zlink_framework_options_t &framework,
                                     const registry_options_t &options)
{
    framework.configure_dispatch ()
      .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
      .trace_log_file (options.log_dir + "/registry-flow.log")
      .trace_label ("cpp-rm-registry");
    framework.enable_registry (options.pub_endpoint, options.router_endpoint);
    auto evidence = std::make_unique<evidence_store_t> (options.rid, options.evidence_file);
    auto *evidence_ptr = evidence.get ();
    auto fault_state = std::make_unique<fault_state_t> ();
    auto *fault_state_ptr = fault_state.get ();
    configure_evidence_dispatch_error_observer (framework, evidence_ptr, fault_state_ptr);
    framework.services ().add_singleton<registry_options_t> (
      std::make_unique<registry_options_t> (options));
    framework.services ().add_singleton<evidence_store_t> (std::move (evidence));
    framework.services ().add_singleton<fault_state_t> (std::move (fault_state));
    if (!options.channel_endpoint.empty ()) {
        framework.add_client_server_channel (api_channel)
          .enable_server (options.channel_endpoint)
          .set_routing_id (zlink::routing_id_t::from (options.rid))
          .use_handler_group (handler_group);
    }
    framework.http ()
      .listen (options.http_endpoint)
      .map_health ("/health")
      .map_get<topology_handler_t> ("/topology")
      .map_get<topology_handler_t> ("/registry/topology")
      .map_get<evidence_handler_t> ("/evidence")
      .map_post<evidence_clear_handler_t> ("/evidence/clear");
    framework.handlers ()
      .group (handler_group)
      .add<profile_request_handler_t> ()
      .add_send<profile_command_handler_t> ();
}

} // namespace zlink::framework::e2e::registry_messaging::registry
