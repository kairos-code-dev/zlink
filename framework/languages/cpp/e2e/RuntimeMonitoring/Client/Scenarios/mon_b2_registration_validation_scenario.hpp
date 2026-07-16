/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "../Support/client_support.hpp"

#include <zlink/http_client.hpp>

#include <chrono>
#include <iostream>
#include <string>

namespace zlink::framework::e2e::runtime_monitoring::client
{

inline std::string post_validation (const std::string &trigger_url, const std::string &path)
{
    auto http = zlink::http_client::client_t::create ()
                  .base_url (trigger_url)
                  .timeout (std::chrono::milliseconds (5000))
                  .build ();
    return http.post (path).async<std::string> ().result ().value ().body;
}

inline void run_mon_b2_registration_validation_scenario (const client_options_t &options)
{
    const auto duplicate_line =
      post_validation (options.trigger_url, "/validation/registration/duplicate-source");
    const auto interval_line =
      post_validation (options.trigger_url, "/validation/registration/interval");
    const auto missing_spot_line =
      post_validation (options.trigger_url, "/validation/registration/missing-spot");
    const auto missing_socket_line =
      post_validation (options.trigger_url, "/validation/registration/missing-socket");

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
