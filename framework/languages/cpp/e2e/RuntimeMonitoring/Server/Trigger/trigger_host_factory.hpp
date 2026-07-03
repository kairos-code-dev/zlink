/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "Support/trigger_options.hpp"
#include "trigger_handlers.hpp"

#include "../Shared/evidence_store.hpp"
#include "../Shared/location_store.hpp"

#include <zlink/framework.hpp>

#include <memory>

namespace zlink::framework::e2e::runtime_monitoring::trigger
{

inline void configure_trigger_host (zlink::framework::zlink_framework_options_t &framework,
                                    const trigger_options_t &options)
{
    auto evidence =
      std::make_unique<server::evidence_store_t> (options.rid, options.evidence_file);
    framework.services ().add_singleton<server::evidence_store_t> (std::move (evidence));
    framework.services ().add_singleton<trigger_options_t> (
      std::make_unique<trigger_options_t> (options));
    server::add_redis_location_store (framework, options.redis_endpoint,
                                      options.redis_key_prefix);
    framework.add_client_server_channel (profile_channel).enable_client ();
    if (!options.http_endpoint.empty ()) {
        framework.http ()
          .listen (options.http_endpoint)
          .map_health ("/health")
          .map_get<server::evidence_handler_t> ("/evidence")
          .map_post<server::evidence_wait_handler_t> ("/evidence/wait")
          .map_post<profile_request_handler_t> ("/profile/request")
          .map_post<service_a_request_handler_t> ("/profile/request/service-a")
          .map_post<service_b_request_handler_t> ("/profile/request/service-b")
          .map_post<throwing_request_handler_t> ("/profile/request/throw")
          .map_get<throw_stderr_handler_t> ("/logs/throw-stderr")
          .map_post<throw_stderr_wait_handler_t> ("/logs/throw-stderr/wait")
          .map_post<duplicate_source_validation_handler_t> (
            "/validation/registration/duplicate-source")
          .map_post<interval_validation_handler_t> ("/validation/registration/interval")
          .map_post<missing_spot_validation_handler_t> (
            "/validation/registration/missing-spot")
          .map_post<missing_socket_validation_handler_t> (
            "/validation/registration/missing-socket")
          .map_post<handshake_failure_handler_t> ("/socket/handshake-failure");
    }
}

inline int run_trigger_host (int argc, char **argv)
{
    const auto options = read_trigger_options ();
    auto app = zlink::framework::app_t::create ();
    app.add_zlink_framework (
      [&] (zlink::framework::zlink_framework_options_t &framework) {
          configure_trigger_host (framework, options);
      });
    return app.run (argc, argv);
}

} // namespace zlink::framework::e2e::runtime_monitoring::trigger
