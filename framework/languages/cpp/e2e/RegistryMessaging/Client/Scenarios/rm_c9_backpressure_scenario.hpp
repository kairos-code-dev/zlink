/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <future>
#include <iostream>
#include <vector>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rm_c9_backpressure_scenario (zlink::framework::channel_client_t &channels)
{
    (void) channels;
    const auto consumer = env_or ("ZLINK_CPP_E2E_BACKPRESSURE_CONSUMER_URL");
    post_raw (consumer, "/profile/backpressure/reset");
    std::vector<std::future<std::string>> sends;
    sends.reserve (32);
    for (int index = 0; index < 32; ++index) {
        sends.push_back (std::async (std::launch::async, [consumer, index] {
            auto outcome = post_json<profile_command_t, backpressure_send_result_t> (
              consumer, "/profile/backpressure/send",
              profile_command_t{.command_id = "rm-c9-slow-" + std::to_string (index)},
              std::chrono::seconds (2));
            return outcome.outcome;
        }));
    }

    int accepted = 0;
    int bounded_failures = 0;
    for (auto &send : sends) {
        const auto outcome = send.get ();
        if (outcome == "Accepted") {
            ++accepted;
        } else if (outcome == "BoundedFailure") {
            ++bounded_failures;
        }
    }
    (void) bounded_failures;
    ensure (accepted > 0, "RM-C9 send pressure did not submit any command");
    wait_provider_evidence_contains ("ProfileCommand", "rm-c9-slow-0", std::chrono::seconds (20));

    std::this_thread::sleep_for (std::chrono::seconds (5));
    auto recovery = post_json<profile_request_t, profile_reply_t> (
      consumer, "/profile/request", profile_request_t{.value = "rm-c9-after"});
    ensure (recovery.value == "profile:rm-c9-after", "RM-C9 recovery reply mismatch");
    wait_provider_evidence_contains ("ProfileRequest", "rm-c9-after", std::chrono::seconds (20));
    std::cout << "scenario RM-C9 passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
