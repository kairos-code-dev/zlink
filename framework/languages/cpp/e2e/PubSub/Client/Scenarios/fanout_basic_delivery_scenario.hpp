/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::pubsub::client
{

inline void run_fanout_basic_delivery_scenario (const std::string &publisher_url)
{
    for (int index = 0; index < 5; ++index) {
        publish (publisher_url, topic_fanout, "warmup-" + std::to_string (index));
    }
    std::this_thread::sleep_for (std::chrono::milliseconds (500));
    for (int index = 0; index < 20; ++index) {
        publish (publisher_url, topic_fanout, "measure-" + std::to_string (index));
    }
    std::vector<std::vector<std::string>> expected;
    for (int index = 0; index < 20; ++index) {
        expected.push_back (accepted_evidence ("measure-" + std::to_string (index)));
    }
    for (const auto &subscriber_url : subscriber_urls ()) {
        (void) wait_for_subscriber_evidence (subscriber_url, expected);
    }
    std::cout << "scenario PS-A1 passed\n";
}

} // namespace zlink::framework::e2e::pubsub::client
