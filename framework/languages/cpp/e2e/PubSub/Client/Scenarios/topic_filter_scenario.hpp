/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::pubsub::client
{

inline void run_topic_filter_scenario (const std::string &publisher_url)
{
    for (int index = 0; index < 8; ++index) {
        publish (publisher_url, topic_alpha, "alpha-" + std::to_string (index));
        publish (publisher_url, topic_beta, "beta-" + std::to_string (index));
    }
    std::cout << "scenario PS-A2 passed\n";
}

} // namespace zlink::framework::e2e::pubsub::client
