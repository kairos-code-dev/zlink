/* SPDX-License-Identifier: MPL-2.0 */

#include "Configuration/registry_options.hpp"

#include <zlink/framework.hpp>

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
    });
    return app.run (argc, argv);
}
