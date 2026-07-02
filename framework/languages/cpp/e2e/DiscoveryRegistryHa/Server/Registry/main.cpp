/* SPDX-License-Identifier: MPL-2.0 */

#include "Configuration/registry_options.hpp"
#include "Endpoints/registry_endpoints.hpp"

#include <zlink/framework.hpp>

#include <memory>

namespace drha_registry = zlink::framework::e2e::discovery_registry_ha::registry;

int main (int argc, char **argv)
{
    const auto options = drha_registry::read_registry_options ();
    auto app = zlink::framework::app_t::create ();
    app.logging ()
      .use_file (options.log_dir + "/" + options.rid + ".log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &framework) {
        framework.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (options.log_dir + "/" + options.rid + "-flow.log")
          .trace_label ("cpp-drha-" + options.rid);
        framework.enable_registry (options.pub_endpoint, options.router_endpoint);
        for (const auto &peer : options.peer_pub_endpoints) {
            framework.add_registry_peer (peer);
        }
        framework.services ().add_singleton<drha_registry::registry_options_t> (
          std::make_unique<drha_registry::registry_options_t> (options));
        framework.http ()
          .listen (options.http_endpoint)
          .map_health ("/health")
          .map_get<drha_registry::topology_snapshot_handler_t> ("/registry/topology")
          .map_post<drha_registry::topology_ready_wait_handler_t> ("/registry/topology/wait")
          .map_post<drha_registry::member_endpoint_wait_handler_t> ("/registry/members/wait")
          .map_post<drha_registry::registry_peer_count_wait_handler_t> (
            "/registry/status/peers/wait");
    });
    return app.run (argc, argv);
}
