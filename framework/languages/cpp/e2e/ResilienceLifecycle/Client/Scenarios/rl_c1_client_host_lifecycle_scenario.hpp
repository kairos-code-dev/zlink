/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "rl_a3_reconnect_storm_scenario.hpp"

#include <iostream>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rl_c1_client_host_lifecycle_probe (zlink::framework::channel_client_t &channels)
{
    run_quick_resilience_scenario (channels);
    std::cout << "scenario RL-C1 client passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
