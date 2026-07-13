/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "rl_a3_reconnect_storm_scenario.hpp"

#include <iostream>

namespace zlink::framework::e2e::resilience_lifecycle::client
{

inline void run_rl_c2_topology_recovery_probe ()
{
    run_quick_resilience_scenario ();
    std::cout << "scenario RL-C2 client passed\n";
}

} // namespace zlink::framework::e2e::resilience_lifecycle::client
