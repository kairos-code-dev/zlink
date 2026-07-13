/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "../Configuration/server_options.hpp"
#include "../Support/server_host.hpp"

#include <zlink/framework.hpp>

#include <exception>
#include <iostream>

namespace rc_server = zlink::framework::e2e::registration_codec::server;

int main (int argc, char **argv)
{
    try {
        rc_server::server_options_t server;
        server.server_mode = "json-only-peer";
        auto app = zlink::framework::app_t::create ();
        app.logging ().use_file (server.log_dir + "/json-only.log")
          .set_min_level (zlink::framework::log_level_t::debug);
        app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
            rc_server::configure_framework (options, server);
        });
        return app.run (argc, argv);
    } catch (const std::exception &error) {
        std::cerr << "registration-codec json-only peer failed: " << error.what () << "\n";
        return 2;
    }
}
