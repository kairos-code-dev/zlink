/* SPDX-License-Identifier: MPL-2.0 */

#include "Configuration/provider_options.hpp"
#include "Handlers/provider_handlers.hpp"
#include "Infrastructure/provider_evidence_store.hpp"

#include "../../Shared/discovery_registry_ha_contracts.hpp"

#include <zlink/framework.hpp>

#include <memory>

namespace drha = zlink::framework::e2e::discovery_registry_ha;
namespace drha_provider = zlink::framework::e2e::discovery_registry_ha::provider;

int main (int argc, char **argv)
{
    const auto options = drha_provider::read_provider_options ();
    auto app = zlink::framework::app_t::create ();
    app.logging ()
      .use_file (options.log_dir + "/" + options.log_name + ".log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &framework) {
        framework.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (options.log_dir + "/" + options.log_name + "-flow.log")
          .trace_label ("cpp-drha-" + options.log_name);
        framework.use_discovery ().add_registry_endpoint (options.registry_router_endpoint);
        framework.services ().add_singleton<drha_provider::provider_evidence_store_t> (
          std::make_unique<drha_provider::provider_evidence_store_t> (options.rid));
        auto channel = framework.add_client_server_channel (drha::api_channel);
        channel.enable_server (options.channel_endpoint)
          .set_routing_id (zlink::routing_id_t::from (options.rid))
          .use_handler_group (drha::handler_group);
        framework.handlers ()
          .group (drha::handler_group)
          .add<drha_provider::profile_request_handler_t> ();
        framework.http ()
          .listen (options.http_endpoint)
          .map_health ("/health")
          .map_get<drha_provider::evidence_snapshot_handler_t> ("/evidence")
          .map_post<drha_provider::evidence_wait_handler_t> ("/evidence/wait");
    });
    return app.run (argc, argv);
}
