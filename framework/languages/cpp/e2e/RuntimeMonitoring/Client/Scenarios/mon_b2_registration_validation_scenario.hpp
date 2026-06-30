/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <zlink/framework.hpp>
#include <zlink/http_client.hpp>

#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace zlink::framework::e2e::runtime_monitoring::client
{

inline std::string capture_error (void (*operation) ())
{
    try {
        operation ();
    } catch (const std::exception &ex) {
        return ex.what ();
    }
    throw std::runtime_error ("expected monitoring validation failure");
}

inline std::string post_validation (const std::string &trigger_url, const std::string &path)
{
    auto http = zlink::http_client::client_t::create ()
                  .base_url (trigger_url)
                  .timeout (std::chrono::milliseconds (5000))
                  .build ();
    return http.post (path).fetch<std::string> ();
}

inline void run_mon_b2_registration_validation_scenario ()
{
    using namespace std::chrono_literals;

    std::string duplicate_line;
    std::string interval_line;
    std::string missing_spot_line;
    std::string missing_socket_line;

    if (const auto trigger_url = env_or ("ZLINK_CPP_E2E_TRIGGER_URL"); !trigger_url.empty ()) {
        duplicate_line = post_validation (trigger_url, "/validation/registration/duplicate-source");
        interval_line = post_validation (trigger_url, "/validation/registration/interval");
        missing_spot_line = post_validation (trigger_url, "/validation/registration/missing-spot");
        missing_socket_line =
          post_validation (trigger_url, "/validation/registration/missing-socket");
    } else {
        duplicate_line = "mon-b2|duplicate=" + capture_error ([] {
            auto app = zlink::framework::app_t::create ();
            app.monitoring ().add_socket_events ("duplicate.source");
            app.monitoring ().add_socket_events ("duplicate.source");
        });

        interval_line = "mon-b2|interval=" + capture_error ([] {
            auto app = zlink::framework::app_t::create ();
            app.monitoring ().add_registry_events ("registry", 0ms);
        });

        missing_spot_line = "mon-b2|missing-spot=" + capture_error ([] {
            using namespace std::chrono_literals;
            auto app = zlink::framework::app_t::create ();
            app.monitoring ().add_spot_events ("missing.spot", 1s);
            app.add_zlink_framework ([] (zlink::framework::zlink_framework_options_t &) {});
        });

        missing_socket_line = "mon-b2|missing-socket=" + capture_error ([] {
            auto app = zlink::framework::app_t::create ();
            app.monitoring ().add_socket_events ("missing.server");
            app.add_zlink_framework ([] (zlink::framework::zlink_framework_options_t &) {});
        });
    }

    const auto duplicate_error = duplicate_line.substr (duplicate_line.find ('=') + 1);
    ensure (contains (duplicate_error, "duplicate monitoring socket source"),
            "MON-B2 duplicate source validation evidence missing.");

    const auto interval_error = interval_line.substr (interval_line.find ('=') + 1);
    ensure (contains (interval_error, "interval must be greater than zero"),
            "MON-B2 interval validation evidence missing.");

    const auto missing_spot_error = missing_spot_line.substr (missing_spot_line.find ('=') + 1);
    ensure (contains (missing_spot_error, "spot monitoring source 'missing.spot' is not registered"),
            "MON-B2 missing spot validation evidence missing.");

    const auto missing_socket_error = missing_socket_line.substr (missing_socket_line.find ('=') + 1);
    ensure (
      contains (missing_socket_error, "socket monitoring source 'missing.server' is not registered"),
      "MON-B2 missing socket validation evidence missing.");

    std::cout << duplicate_line << '\n';
    std::cout << interval_line << '\n';
    std::cout << missing_spot_line << '\n';
    std::cout << missing_socket_line << '\n';
    std::cout << "scenario MON-B2 passed" << '\n';
}

} // namespace zlink::framework::e2e::runtime_monitoring::client
