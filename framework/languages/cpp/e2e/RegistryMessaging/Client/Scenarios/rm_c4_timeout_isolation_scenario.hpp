/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rm_c4_timeout_isolation_scenario (zlink::framework::channel_client_t &channels)
{
    (void) channels;
    const auto consumer = env_or ("ZLINK_CPP_E2E_DISCOVERY_CONSUMER_URL");
    const auto timeout = post_json<profile_request_t, request_failure_result_t> (
      consumer, "/profile/slow-request", profile_request_t{.value = "slow"});
    ensure (timeout.failed, "RM-C4 expected the slow request to time out");
    auto after = post_json<profile_request_t, profile_reply_t> (
      consumer, "/profile/request", profile_request_t{.value = "rm-c4-after-timeout"});
    ensure (after.value == "profile:rm-c4-after-timeout", "RM-C4 post-timeout reply mismatch");
    std::this_thread::sleep_for (std::chrono::milliseconds (1100));
    auto later = post_json<profile_request_t, profile_reply_t> (
      consumer, "/profile/request", profile_request_t{.value = "rm-c4-later"});
    ensure (later.value == "profile:rm-c4-later", "RM-C4 later reply mismatch");
    wait_provider_evidence_contains ("ProfileRequest", "rm-c4-after-timeout",
                                     std::chrono::seconds (10));
    wait_provider_evidence_contains ("ProfileRequest", "rm-c4-later", std::chrono::seconds (10));
    std::cout << "scenario RM-C4 passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
