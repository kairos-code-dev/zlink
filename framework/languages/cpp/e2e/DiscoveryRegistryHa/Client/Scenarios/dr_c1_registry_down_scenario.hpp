/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::discovery_registry_ha::client
{

inline void run_dr_c1_registry_down_scenario ()
{
    const auto reg1_url = env_or ("ZLINK_CPP_DRHA_REG1_URL");
    const auto reg2_url = env_or ("ZLINK_CPP_DRHA_REG2_URL");
    const auto consumer_url = env_or ("ZLINK_CPP_DRHA_REG1_CONSUMER_URL");
    const auto provider_a_url = env_or ("ZLINK_CPP_DRHA_PROVIDER_A_URL");
    const auto provider_b_url = env_or ("ZLINK_CPP_DRHA_PROVIDER_B_URL");
    const auto api_a_endpoint = env_or ("ZLINK_CPP_DRHA_API_A_ENDPOINT");

    ensure (!reg1_url.empty (), "ZLINK_CPP_DRHA_REG1_URL is required");
    ensure (!reg2_url.empty (), "ZLINK_CPP_DRHA_REG2_URL is required");
    ensure (!consumer_url.empty (), "ZLINK_CPP_DRHA_REG1_CONSUMER_URL is required");
    ensure (!provider_a_url.empty (), "ZLINK_CPP_DRHA_PROVIDER_A_URL is required");
    ensure (!provider_b_url.empty (), "ZLINK_CPP_DRHA_PROVIDER_B_URL is required");
    ensure (!api_a_endpoint.empty (), "ZLINK_CPP_DRHA_API_A_ENDPOINT is required");

    wait_member_endpoint (reg1_url, api_a_endpoint);
    verify_profile_request ("dr-c1", "survivor", consumer_url, provider_a_url, provider_b_url,
                            "dr-c1");
    ensure (health_probe_fails (reg2_url),
            "DR-C1 dead registry endpoint did not fail within the bounded timeout");

    std::cout << "scenario DR-C1 passed\n";
}

} // namespace zlink::framework::e2e::discovery_registry_ha::client
