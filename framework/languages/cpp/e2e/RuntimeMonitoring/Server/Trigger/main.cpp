/* SPDX-License-Identifier: MPL-2.0 */

#include "Support/trigger_options.hpp"
#include "trigger_endpoints.hpp"

#include "../Shared/evidence_store.hpp"

#include <zlink/framework.hpp>

#include <memory>

namespace rm = zlink::framework::e2e::runtime_monitoring;
namespace rm_server = zlink::framework::e2e::runtime_monitoring::server;
namespace rm_trigger = zlink::framework::e2e::runtime_monitoring::trigger;

int main (int argc, char **argv)
{
    const auto options = rm_trigger::read_trigger_options ();
    auto app = zlink::framework::app_t::create ();
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &framework) {
        auto evidence = std::make_unique<rm_server::evidence_store_t> (options.rid,
                                                                       options.evidence_file);
        framework.services ().add_singleton<rm_server::evidence_store_t> (std::move (evidence));
        framework.services ().add_singleton<rm_trigger::trigger_options_t> (
          std::make_unique<rm_trigger::trigger_options_t> (options));
        framework.codecs ().add_json ();
        framework.codecs ().add_json<rm::profile_request_t, rm::profile_reply_t> ();
        framework.codecs ().add_json<rm::evidence_wait_request_t, std::vector<std::string>> ();
        framework.use_discovery ().add_registry_endpoint (options.registry_router);
        framework.add_client_server_channel (rm::profile_channel).enable_client ();
        if (!options.http_endpoint.empty ()) {
            framework.http ()
              .listen (options.http_endpoint)
              .map_health ("/health")
              .map_get<rm_server::evidence_handler_t> ("/evidence")
              .map_post<rm_server::evidence_wait_handler_t> ("/evidence/wait")
              .map_post<rm_trigger::profile_request_handler_t> ("/profile/request")
              .map_post<rm_trigger::service_a_request_handler_t> ("/profile/request/service-a")
              .map_post<rm_trigger::service_b_request_handler_t> ("/profile/request/service-b")
              .map_post<rm_trigger::throwing_request_handler_t> ("/profile/request/throw")
              .map_get<rm_trigger::throw_stderr_handler_t> ("/logs/throw-stderr")
              .map_post<rm_trigger::throw_stderr_wait_handler_t> ("/logs/throw-stderr/wait")
              .map_post<rm_trigger::duplicate_source_validation_handler_t> (
                "/validation/registration/duplicate-source")
              .map_post<rm_trigger::interval_validation_handler_t> (
                "/validation/registration/interval")
              .map_post<rm_trigger::missing_spot_validation_handler_t> (
                "/validation/registration/missing-spot")
              .map_post<rm_trigger::missing_socket_validation_handler_t> (
                "/validation/registration/missing-socket")
              .map_post<rm_trigger::handshake_failure_handler_t> ("/socket/handshake-failure");
        }
    });
    return app.run (argc, argv);
}
