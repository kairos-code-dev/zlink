/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "rl_b4_runtime_drain_scenario.hpp"

#include <iostream>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rl_a4_b4_b5_b6_drain_scenario (zlink::framework::channel_client_t &channels)
{
    run_drain_restore_scenario (channels);
    std::cout << "scenario RL-A4 client passed\n";
    std::cout << "scenario RL-B6 client passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
