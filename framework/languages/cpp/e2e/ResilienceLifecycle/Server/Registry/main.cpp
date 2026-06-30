/* SPDX-License-Identifier: MPL-2.0 */

#include "Configuration/registry_options.hpp"
#include "Endpoints/registry_endpoints.hpp"

#include <zlink/framework.hpp>

#include <memory>

namespace rm_registry = zlink::framework::e2e::registry_messaging::registry;

int main (int argc, char **argv)
{
    const auto options = rm_registry::read_registry_options ();
    auto app = zlink::framework::app_t::create ();
    app.logging ()
      .use_file (options.log_dir + "/registry.log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &framework) {
        framework.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (options.log_dir + "/registry-flow.log")
          .trace_label ("cpp-rm-registry");
        framework.enable_registry (options.pub_endpoint, options.router_endpoint);
        framework.services ().add_singleton<rm_registry::registry_options_t> (
          std::make_unique<rm_registry::registry_options_t> (options));
        framework.http ()
          .listen (options.http_endpoint)
          .map_health ("/health")
          .map_get<rm_registry::topology_handler_t> ("/registry/topology");
    });
    return app.run (argc, argv);
}
