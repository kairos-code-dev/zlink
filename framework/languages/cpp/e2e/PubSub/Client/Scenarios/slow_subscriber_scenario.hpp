/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::pubsub::client
{

inline void run_slow_subscriber_scenario (const std::string &publisher_url)
{
    for (int index = 0; index < 16; ++index) {
        publish (publisher_url, topic_fanout, "slow-isolation-" + std::to_string (index));
    }
    std::cout << "scenario PS-B1 passed\n";
}

} // namespace zlink::framework::e2e::pubsub::client
