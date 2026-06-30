/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rm_c4_timeout_isolation_scenario (zlink::framework::channel_client_t &channels)
{
    auto slow = channels.request (api_channel, profile_request_t{.value = "slow"})
                  .timeout (std::chrono::milliseconds (100))
                  .async<profile_reply_t> ();
    ensure (!slow.result ().has_value (), "RM-C4 slow request unexpectedly succeeded");
    ensure (slow.result ().error_kind () == zlink::framework::framework_error_kind_t::timeout,
            "RM-C4 slow request failed with unexpected public error kind");
    auto after = channels.request (api_channel, profile_request_t{.value = "rm-c4-after-timeout"})
                   .timeout (std::chrono::milliseconds (2000))
                   .async<profile_reply_t> ();
    ensure (after.result ().has_value (), "RM-C4 post-timeout request failed");
    std::this_thread::sleep_for (std::chrono::milliseconds (1100));
    auto later = channels.request (api_channel, profile_request_t{.value = "rm-c4-later"})
                   .timeout (std::chrono::milliseconds (2000))
                   .async<profile_reply_t> ();
    ensure (later.result ().has_value (), "RM-C4 later request failed");
    wait_provider_evidence_contains ("ProfileRequest", "rm-c4-after-timeout",
                                     std::chrono::seconds (10));
    wait_provider_evidence_contains ("ProfileRequest", "rm-c4-later", std::chrono::seconds (10));
    std::cout << "scenario RM-C4 passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
