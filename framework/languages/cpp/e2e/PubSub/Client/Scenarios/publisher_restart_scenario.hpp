/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::pubsub::client
{

inline void run_publisher_restart_scenario (const std::string &publisher_url)
{
    publish (publisher_url, topic_fanout, "before-publisher-restart-1");

    post_empty (publisher_url, "/shutdown");
    wait_http_health ("publisher", publisher_url, false);
    ensure (!try_post_empty (publisher_url,
                             "/publish/event?topic=" + url_encode (topic_fanout)
                               + "&value=during-publisher-down"),
            "PS-B2 expected publish attempt to fail while publisher is down");

    auto restarted_publisher = start_publisher_process ("pub-restart", publisher_url);
    write_pid_file (env_or ("ZLINK_CPP_E2E_RESTARTED_PUBLISHER_PID_FILE"),
                    restarted_publisher.pid ());
    for (int index = 20; index <= 42; ++index) {
        publish (publisher_url, topic_fanout,
                 "after-publisher-restart-" + std::to_string (index));
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
    std::vector<std::vector<std::string>> expected;
    for (int index = 20; index <= 42; ++index) {
        expected.push_back (
          accepted_evidence ("after-publisher-restart-" + std::to_string (index)));
    }
    for (const auto &subscriber_url : subscriber_urls ()) {
        (void) wait_for_subscriber_evidence (subscriber_url, expected);
    }
    std::cout << "scenario PS-B2 passed\n";
}

} // namespace zlink::framework::e2e::pubsub::client
