/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework.hpp>

#include <stdexcept>
#include <string>

inline void load_spot_service_config (zlink::framework::app_t &app,
                                      int argc,
                                      char **argv,
                                      const char *role)
{
    app.config ().load_cli (argc, argv);
    const auto path = app.config ().model ().get ("config");
    if (!path) {
        throw std::runtime_error (std::string ("SpotService ") + role
                                  + " requires --config=<path>");
    }
    app.config ().load_json (*path);
}
