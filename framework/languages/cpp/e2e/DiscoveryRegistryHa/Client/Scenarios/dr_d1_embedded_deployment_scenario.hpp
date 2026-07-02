/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::discovery_registry_ha::client
{

inline void run_dr_d1_embedded_deployment_scenario ()
{
    const auto embedded_url = env_or ("ZLINK_CPP_DRHA_EMBEDDED_URL");
    const auto consumer_url = env_or ("ZLINK_CPP_DRHA_EMBEDDED_CONSUMER_URL");

    ensure (!embedded_url.empty (), "ZLINK_CPP_DRHA_EMBEDDED_URL is required");
    ensure (!consumer_url.empty (), "ZLINK_CPP_DRHA_EMBEDDED_CONSUMER_URL is required");

    verify_profile_request_from_provider ("dr-d1", "embedded", consumer_url, embedded_url,
                                          "embedded-api", "dr-d1");

    std::cout << "scenario DR-D1 passed\n";
}

} // namespace zlink::framework::e2e::discovery_registry_ha::client
