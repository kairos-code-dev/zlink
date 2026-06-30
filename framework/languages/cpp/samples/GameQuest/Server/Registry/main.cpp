/* SPDX-License-Identifier: MPL-2.0 */

#include "../Configuration/sample_names.hpp"

#include <zlink/framework.hpp>

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    using namespace zlink::samples::gamequest;

    const sample_topology_t topology;
    auto app = app_t::create ();
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (gamequest_log_dir () + "/flow-registry.log")
          .trace_label ("gamequest-registry");
        options.enable_registry (topology.registry_pub_endpoint, topology.registry_router_endpoint);
    });
    return app.run (argc, argv);
}
