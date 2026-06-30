/* SPDX-License-Identifier: MPL-2.0 */

#include "Configuration/registry_options.hpp"
#include "registry_host_factory.hpp"

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
        rm_registry::configure_registry_host (framework, options);
    });
    return app.run (argc, argv);
}
