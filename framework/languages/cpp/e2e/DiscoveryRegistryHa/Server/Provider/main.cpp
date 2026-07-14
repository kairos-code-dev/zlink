/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "Configuration/provider_options.hpp"
#include "Handlers/provider_handlers.hpp"
#include "Infrastructure/provider_evidence_store.hpp"

#include "../../Shared/store_failure_contracts.hpp"
#include "../Shared/location_store.hpp"

#include <zlink/framework.hpp>

#include <memory>

namespace sf = zlink::framework::e2e::store_failure;
namespace sf_provider = zlink::framework::e2e::store_failure::provider;

int main (int argc, char **argv)
{
    const auto options = sf_provider::read_provider_options ();
    auto app = zlink::framework::app_t::create ();
    auto lifecycle_owner = std::make_unique<sf_provider::provider_lifecycle_control_t> ();
    lifecycle_owner->drain = [&app] (std::chrono::milliseconds deadline) {
        return app.drain (deadline).result ().value ();
    };
    lifecycle_owner->request_stop = [&app] { app.request_stop (); };
    app.logging ()
      .use_file (options.log_dir + "/" + options.log_name + ".log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &framework) {
        framework.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (options.log_dir + "/" + options.log_name + "-flow.log")
          .trace_label ("cpp-store-failure-" + options.log_name);
        sf::server::add_redis_location_store (framework, options.redis_endpoint,
                                              options.redis_key_prefix);
        framework.services ().add_singleton<sf_provider::provider_evidence_store_t> (
          std::make_unique<sf_provider::provider_evidence_store_t> (options.rid));
        framework.services ().add_singleton<sf_provider::provider_lifecycle_control_t> (
          std::move (lifecycle_owner));
        auto channel = framework.add_client_server_channel (sf::api_channel);
        channel.enable_server (options.channel_endpoint)
          .set_routing_id (zlink::routing_id_t::from (options.rid))
          .use_handler_group (sf::handler_group);
        framework.handlers ()
          .group (sf::handler_group)
          .add<sf_provider::profile_request_handler_t> ();
        framework.http ()
          .listen (options.http_endpoint)
          .map_health ("/health")
          .map_get<sf_provider::query_status_handler_t> ("/query/status")
          .map_get<sf_provider::evidence_snapshot_handler_t> ("/evidence")
          .map_post<sf_provider::evidence_wait_handler_t> ("/evidence/wait")
          .map_post<sf_provider::drain_handler_t> ("/drain")
          .map_post<sf_provider::shutdown_handler_t> ("/shutdown")
          .map_post<sf_provider::crash_handler_t> ("/admin/crash");
    });
    return app.run (argc, argv);
}
