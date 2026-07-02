/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace zlink::framework::e2e::discovery_registry_ha::client
{

struct dr_b3_registry_case_t
{
    std::string name;
    std::string registry_url;
    std::string consumer_url;
};

inline void run_dr_b3_peer_flapping_scenario ()
{
    const auto reg1_url = env_or ("ZLINK_CPP_DRHA_REG1_URL");
    const auto reg2_url = env_or ("ZLINK_CPP_DRHA_REG2_URL");
    const auto reg1_consumer_url = env_or ("ZLINK_CPP_DRHA_REG1_CONSUMER_URL");
    const auto reg2_consumer_url = env_or ("ZLINK_CPP_DRHA_REG2_CONSUMER_URL");
    const auto provider_a_url = env_or ("ZLINK_CPP_DRHA_PROVIDER_A_URL");
    const auto provider_b_url = env_or ("ZLINK_CPP_DRHA_PROVIDER_B_URL");
    const auto api_a_endpoint = env_or ("ZLINK_CPP_DRHA_API_A_ENDPOINT");

    ensure (!reg1_url.empty (), "ZLINK_CPP_DRHA_REG1_URL is required");
    ensure (!reg2_url.empty (), "ZLINK_CPP_DRHA_REG2_URL is required");
    ensure (!reg1_consumer_url.empty (), "ZLINK_CPP_DRHA_REG1_CONSUMER_URL is required");
    ensure (!reg2_consumer_url.empty (), "ZLINK_CPP_DRHA_REG2_CONSUMER_URL is required");
    ensure (!provider_a_url.empty (), "ZLINK_CPP_DRHA_PROVIDER_A_URL is required");
    ensure (!provider_b_url.empty (), "ZLINK_CPP_DRHA_PROVIDER_B_URL is required");
    ensure (!api_a_endpoint.empty (), "ZLINK_CPP_DRHA_API_A_ENDPOINT is required");

    const std::vector<dr_b3_registry_case_t> cases{
      {"reg-2", reg2_url, reg2_consumer_url},
      {"survivor", reg1_url, reg1_consumer_url},
    };
    for (const auto &registry_case : cases) {
        wait_member_endpoint (registry_case.registry_url, api_a_endpoint);
        verify_profile_request ("dr-b3", registry_case.name, registry_case.consumer_url,
                                provider_a_url, provider_b_url, "dr-b3");
    }

    std::cout << "scenario DR-B3 passed\n";
}

} // namespace zlink::framework::e2e::discovery_registry_ha::client
