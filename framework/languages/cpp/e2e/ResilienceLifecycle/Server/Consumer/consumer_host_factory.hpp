/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "Configuration/consumer_options.hpp"
#include "Endpoints/consumer_endpoints.hpp"

#include <zlink/framework.hpp>

#include <memory>

namespace zlink::framework::e2e::resilience_lifecycle::consumer
{

inline void configure_consumer_host (zlink::framework::zlink_framework_options_t &framework,
                                     const consumer_options_t &options)
{
    framework.configure_dispatch ()
      .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
      .trace_log_file (options.log_dir + "/consumer-flow.log")
      .trace_label ("consumer");
    framework.services ().add_singleton<consumer_options_t> (
      std::make_unique<consumer_options_t> (options));
    auto channel = framework.add_client_server_channel (api_channel);
    framework.use_discovery ().add_registry_endpoint (options.registry_router);
    channel.enable_client ();
    framework.http ()
      .listen (options.http_endpoint)
      .configure_server ([] (zlink::framework::http_server_options_builder_t &server) {
          server.set_max_request_body_size (2 * 1024 * 1024);
      })
      .map_health ("/health")
      .map_post<profile_request_handler_t> ("/profile/request")
      .map_post<slow_request_handler_t> ("/profile/request/timeout/100")
      .map_post<missing_request_handler_t> ("/profile/request/missing")
      .map_post<profile_command_handler_t> ("/profile/command")
      .map_post<new_client_profile_request_handler_t> ("/profile/request/new-client");
}

} // namespace zlink::framework::e2e::resilience_lifecycle::consumer
