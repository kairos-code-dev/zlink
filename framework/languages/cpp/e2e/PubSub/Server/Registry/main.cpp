/* SPDX-License-Identifier: MPL-2.0 */

#include "Configuration/registry_options.hpp"

namespace ps_registry = zlink::framework::e2e::pubsub::server::registry;
namespace ps_server = zlink::framework::e2e::pubsub::server;

int main (int argc, char **argv)
{
    ps_registry::registry_options_t pubsub;
    auto app = zlink::framework::app_t::create ();
    app.logging ()
      .use_file (pubsub.log_dir + "/registry.log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.services ().add_singleton<ps_server::operational_evidence_store_t> (
          std::make_unique<ps_server::operational_evidence_store_t> ("registry"));
        ps_server::configure_flow (options, pubsub.log_dir, "registry");
        options.enable_registry (pubsub.pub_endpoint, pubsub.router_endpoint);
        if (!pubsub.http_endpoint.empty ()) {
            options.http ()
              .listen (pubsub.http_endpoint)
              .map_health ("/health")
              .map_get<ps_server::operational_evidence_handler_t> ("/evidence")
              .map_post<ps_server::operational_evidence_clear_handler_t> ("/evidence/clear")
              .map_post<ps_server::operational_shutdown_handler_t> ("/shutdown");
        }
    });
    return app.run (argc, argv);
}
