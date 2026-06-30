/* SPDX-License-Identifier: MPL-2.0 */

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
        auto app = zlink::framework::app_t::create ();
        app.logging ().use_file (server.log_dir + "/invalid.log")
          .set_min_level (zlink::framework::log_level_t::debug);
        app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
            rc_server::configure_framework (options, server);
        });
        return app.run (argc, argv);
    } catch (const std::exception &error) {
        std::cerr << "registration-codec invalid server failed: " << error.what () << "\n";
        return 2;
    }
}
