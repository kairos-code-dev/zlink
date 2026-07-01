/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "Handlers/registry_event_recorders.hpp"
#include "Support/registry_options.hpp"

#include "../Shared/evidence_store.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <csignal>
#include <memory>
#include <thread>

namespace zlink::framework::e2e::runtime_monitoring::registry
{

class registry_shutdown_handler_t
{
  public:
    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &)
    {
        std::thread ([] {
            std::this_thread::sleep_for (std::chrono::milliseconds (20));
            std::raise (SIGTERM);
        }).detach ();
        zlink::framework::http_response_t response;
        response.body = R"({"status":"stopping"})";
        return response;
    }
};

inline void configure_registry_host (zlink::framework::zlink_framework_options_t &framework,
                                     const registry_options_t &options)
{
    auto evidence =
      std::make_unique<server::evidence_store_t> (options.rid, options.evidence_file);
    auto *evidence_ptr = evidence.get ();
    framework.services ().add_singleton<server::evidence_store_t> (std::move (evidence));
    framework.enable_registry (options.pub_endpoint, options.router_endpoint);
    if (!options.log_dir.empty ()) {
        framework.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (options.log_dir + "/registry-flow.log")
          .trace_label ("cpp-mon-registry");
    }
    framework.monitoring ()
      .add_registry_events ("registry", std::chrono::milliseconds (100))
      .on<zlink::framework::registry_event_payload_t> (
        [evidence_ptr] (const zlink::framework::registry_event_payload_t &event) {
            record_registry_event (*evidence_ptr, event);
        });
    if (!options.http_endpoint.empty ()) {
        framework.http ()
          .listen (options.http_endpoint)
          .map_health ("/health")
          .map_get<server::evidence_handler_t> ("/evidence")
          .map_post<server::evidence_wait_handler_t> ("/evidence/wait")
          .map_post<registry_shutdown_handler_t> ("/shutdown");
    }
}

inline int run_registry_host (int argc, char **argv)
{
    const auto options = read_registry_options ();
    auto app = zlink::framework::app_t::create ();
    app.add_zlink_framework (
      [&] (zlink::framework::zlink_framework_options_t &framework) {
          configure_registry_host (framework, options);
      });
    return app.run (argc, argv);
}

} // namespace zlink::framework::e2e::runtime_monitoring::registry
