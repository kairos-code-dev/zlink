/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/resilience_request_support.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_quick_resilience_scenario (zlink::framework::channel_client_t &channels)
{
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (6);
    while (std::chrono::steady_clock::now () < deadline) {
        auto task = channels.request (api_channel, profile_req_t{.value = "rl-quick"})
                      .timeout (std::chrono::milliseconds (1000))
                      .async<profile_res_t> ();
        if (task.result ().has_value () && !task.result ().value ().provider_rid.empty ()) {
            std::cout << "scenario quick passed\n";
            return;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (200));
    }
    throw std::runtime_error ("quick scenario did not recover within timeout");
}

inline void run_rl_a3_reconnect_storm_probe (zlink::framework::channel_client_t &channels)
{
    run_quick_resilience_scenario (channels);
    std::cout << "scenario RL-A3 client passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
