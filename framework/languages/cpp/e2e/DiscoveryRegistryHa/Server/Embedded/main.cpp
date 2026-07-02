/* SPDX-License-Identifier: MPL-2.0 */

#include "Configuration/embedded_options.hpp"

#include "../Provider/Handlers/provider_handlers.hpp"
#include "../Provider/Infrastructure/provider_evidence_store.hpp"
#include "../Registry/Configuration/registry_options.hpp"
#include "../Registry/Endpoints/registry_endpoints.hpp"

#include "../../Shared/discovery_registry_ha_contracts.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <csignal>
#include <memory>
#include <thread>

namespace drha = zlink::framework::e2e::discovery_registry_ha;
namespace drha_embedded = zlink::framework::e2e::discovery_registry_ha::embedded;
namespace drha_provider = zlink::framework::e2e::discovery_registry_ha::provider;
namespace drha_registry = zlink::framework::e2e::discovery_registry_ha::registry;

namespace
{

class shutdown_handler_t
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

drha_registry::registry_options_t registry_options_from (const drha_embedded::embedded_options_t &options)
{
    drha_registry::registry_options_t registry_options;
    registry_options.rid = options.rid;
    registry_options.pub_endpoint = options.registry_pub_endpoint;
    registry_options.router_endpoint = options.registry_router_endpoint;
    registry_options.http_endpoint = options.http_endpoint;
    registry_options.log_dir = options.log_dir;
    registry_options.peer_pub_endpoints = options.peer_pub_endpoints;
    return registry_options;
}

} // namespace

int main (int argc, char **argv)
{
    const auto options = drha_embedded::read_embedded_options ();
    auto registry_options = registry_options_from (options);
    auto app = zlink::framework::app_t::create ();
    app.logging ()
      .use_file (options.log_dir + "/" + options.log_name + ".log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &framework) {
        framework.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (options.log_dir + "/" + options.log_name + "-flow.log")
          .trace_label ("cpp-drha-" + options.log_name);
        framework.enable_registry (options.registry_pub_endpoint, options.registry_router_endpoint);
        for (const auto &peer : options.peer_pub_endpoints) {
            framework.add_registry_peer (peer);
        }
        for (const auto &endpoint : options.discovery_endpoints) {
            framework.use_discovery ().add_registry_endpoint (endpoint);
        }
        framework.services ().add_singleton<drha_registry::registry_options_t> (
          std::make_unique<drha_registry::registry_options_t> (registry_options));
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
          .map_post<drha_provider::evidence_wait_handler_t> ("/evidence/wait")
          .map_get<drha_registry::topology_snapshot_handler_t> ("/registry/topology")
          .map_post<drha_registry::topology_ready_wait_handler_t> ("/registry/topology/wait")
          .map_post<drha_registry::member_endpoint_wait_handler_t> ("/registry/members/wait")
          .map_post<shutdown_handler_t> ("/shutdown");
    });
    return app.run (argc, argv);
}
