/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::pubsub::client
{

inline void run_publisher_restart_scenario (const std::string &publisher_url)
{
    const auto phase = env_or ("ZLINK_CPP_E2E_PUBLISHER_RESTART_PHASE", "before");
    if (phase == "before") {
        publish (publisher_url, topic_fanout, "before-publisher-restart-1");
    } else if (phase == "after") {
        for (int index = 20; index <= 42; ++index) {
            publish (publisher_url, topic_fanout,
                     "after-publisher-restart-" + std::to_string (index));
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
        }
    } else {
        throw std::runtime_error ("unknown publisher restart phase " + phase);
    }
    std::cout << "scenario PS-B2 passed\n";
}

} // namespace zlink::framework::e2e::pubsub::client
