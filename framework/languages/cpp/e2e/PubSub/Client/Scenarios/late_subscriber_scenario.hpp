/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::pubsub::client
{

inline void run_late_subscriber_scenario (const std::string &publisher_url)
{
    for (int index = 0; index < 5; ++index) {
        publish (publisher_url, topic_fanout, "before-late-" + std::to_string (index));
    }
    touch_file (env_or ("ZLINK_CPP_E2E_READY_FILE"));
    wait_for_file (env_or ("ZLINK_CPP_E2E_CONTINUE_FILE"));
    std::this_thread::sleep_for (std::chrono::milliseconds (500));
    for (int index = 0; index < 8; ++index) {
        publish (publisher_url, topic_fanout, "after-late-" + std::to_string (index));
    }
    std::cout << "scenario PS-A3 passed\n";
}

} // namespace zlink::framework::e2e::pubsub::client
