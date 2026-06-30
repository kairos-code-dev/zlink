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
    std::vector<std::future<bool>> sends;
    sends.reserve (32);
    for (int index = 0; index < 32; ++index) {
        sends.push_back (std::async (std::launch::async, [&channels, index] {
            auto task = channels
                          .send ("registry.messaging.api.manual.backpressure",
                                 profile_command_t{.command_id = "rm-c9-slow-"
                                                                 + std::to_string (index)})
                          .async ();
            return task.result ().has_value ();
        }));
    }

    int accepted = 0;
    for (auto &send : sends) {
        if (send.get ()) {
            ++accepted;
        }
    }
    ensure (accepted > 0, "RM-C9 send pressure did not submit any command");
    wait_provider_evidence_contains ("ProfileCommand", "rm-c9-slow-0", std::chrono::seconds (20));

    std::this_thread::sleep_for (std::chrono::seconds (5));
    auto recovery = channels
                      .request ("registry.messaging.api.manual.backpressure",
                                profile_request_t{.value = "rm-c9-after"})
                      .timeout (std::chrono::milliseconds (2000))
                      .async<profile_reply_t> ();
    ensure (recovery.result ().has_value (), "RM-C9 recovery request failed");
    ensure (recovery.result ().value ().value == "profile:rm-c9-after",
            "RM-C9 recovery reply mismatch");
    wait_provider_evidence_contains ("ProfileRequest", "rm-c9-after", std::chrono::seconds (20));
    std::cout << "scenario RM-C9 passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
