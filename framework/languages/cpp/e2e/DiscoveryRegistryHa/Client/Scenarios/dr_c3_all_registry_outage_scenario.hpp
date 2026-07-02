/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::discovery_registry_ha::client
{

inline void run_dr_c3_all_registry_outage_scenario ()
{
    const auto phase = env_or ("ZLINK_CPP_DRHA_C3_PHASE", "all");
    const auto survivor_consumer_url = env_or ("ZLINK_CPP_DRHA_REG1_CONSUMER_URL");
    const auto recovered_consumer_url = env_or ("ZLINK_CPP_DRHA_REG2_CONSUMER_URL");
    const auto provider_a_url = env_or ("ZLINK_CPP_DRHA_PROVIDER_A_URL");
    const auto provider_b_url = env_or ("ZLINK_CPP_DRHA_PROVIDER_B_URL");
    const auto provider_c_url = env_or ("ZLINK_CPP_DRHA_PROVIDER_C_URL");
    const auto provider_c_endpoint = env_or ("ZLINK_CPP_DRHA_PROVIDER_C_ENDPOINT");
    const auto reg2_url = env_or ("ZLINK_CPP_DRHA_REG2_URL");

    ensure (!survivor_consumer_url.empty (), "ZLINK_CPP_DRHA_REG1_CONSUMER_URL is required");
    ensure (!recovered_consumer_url.empty (), "ZLINK_CPP_DRHA_REG2_CONSUMER_URL is required");
    ensure (!provider_a_url.empty (), "ZLINK_CPP_DRHA_PROVIDER_A_URL is required");
    ensure (!provider_b_url.empty (), "ZLINK_CPP_DRHA_PROVIDER_B_URL is required");
    ensure (!provider_c_url.empty (), "ZLINK_CPP_DRHA_PROVIDER_C_URL is required");
    ensure (!provider_c_endpoint.empty (), "ZLINK_CPP_DRHA_PROVIDER_C_ENDPOINT is required");
    ensure (!reg2_url.empty (), "ZLINK_CPP_DRHA_REG2_URL is required");

    if (phase == "before" || phase == "all") {
        verify_profile_request ("dr-c3", "before", survivor_consumer_url, provider_a_url,
                                provider_b_url, "dr-c3");
        if (phase == "before") {
            std::cout << "scenario DR-C3 before passed\n";
            return;
        }
    }

    if (phase == "during" || phase == "all") {
        verify_profile_request ("dr-c3", "during", survivor_consumer_url, provider_a_url,
                                provider_b_url, "dr-c3");
        if (phase == "during") {
            std::cout << "scenario DR-C3 during passed\n";
            return;
        }
    }

    wait_member_endpoint (reg2_url, provider_c_endpoint);
    verify_profile_request_from_provider ("dr-c3", "after", recovered_consumer_url,
                                          provider_c_url, "api-c", "dr-c3");

    std::cout << "scenario DR-C3 passed\n";
}

} // namespace zlink::framework::e2e::discovery_registry_ha::client
