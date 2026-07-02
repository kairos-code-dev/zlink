/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::discovery_registry_ha::client
{

inline void run_dr_c2_registry_recovery_scenario ()
{
    const auto reg2_url = env_or ("ZLINK_CPP_DRHA_REG2_URL");
    const auto consumer_url = env_or ("ZLINK_CPP_DRHA_REG2_CONSUMER_URL");
    const auto provider_a_url = env_or ("ZLINK_CPP_DRHA_PROVIDER_A_URL");
    const auto provider_b_url = env_or ("ZLINK_CPP_DRHA_PROVIDER_B_URL");
    const auto api_a_endpoint = env_or ("ZLINK_CPP_DRHA_API_A_ENDPOINT");

    ensure (!reg2_url.empty (), "ZLINK_CPP_DRHA_REG2_URL is required");
    ensure (!consumer_url.empty (), "ZLINK_CPP_DRHA_REG2_CONSUMER_URL is required");
    ensure (!provider_a_url.empty (), "ZLINK_CPP_DRHA_PROVIDER_A_URL is required");
    ensure (!provider_b_url.empty (), "ZLINK_CPP_DRHA_PROVIDER_B_URL is required");
    ensure (!api_a_endpoint.empty (), "ZLINK_CPP_DRHA_API_A_ENDPOINT is required");

    wait_member_endpoint (reg2_url, api_a_endpoint);
    verify_profile_request ("dr-c2", "recovered", consumer_url, provider_a_url, provider_b_url,
                            "dr-c2");

    std::cout << "scenario DR-C2 passed\n";
}

} // namespace zlink::framework::e2e::discovery_registry_ha::client
