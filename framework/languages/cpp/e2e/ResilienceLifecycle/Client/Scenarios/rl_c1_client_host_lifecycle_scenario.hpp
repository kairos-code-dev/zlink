/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "../Support/lifecycle_api_result.hpp"

#include <zlink/http_client.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace zlink::framework::e2e::resilience_lifecycle::client
{

inline bool provider_evidence_prefix_exists (const client_options_t &options,
                                             const std::string &prefix)
{
    const auto evidence_a = fetch_evidence (options.http_a_endpoint);
    const auto evidence_b = fetch_evidence (options.http_b_endpoint);
    for (const auto &entry : evidence_a.entries) {
        if (entry.marker == "ProfileReq" && entry.value.rfind (prefix, 0) == 0) {
            return true;
        }
    }
    for (const auto &entry : evidence_b.entries) {
        if (entry.marker == "ProfileReq" && entry.value.rfind (prefix, 0) == 0) {
            return true;
        }
    }
    return false;
}

inline void wait_provider_evidence_prefix (const client_options_t &options,
                                           const std::string &prefix)
{
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (15);
    while (std::chrono::steady_clock::now () < deadline) {
        if (provider_evidence_prefix_exists (options, prefix)) {
            return;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
    throw std::runtime_error ("RL-C1 did not record expected provider evidence " + prefix);
}

inline void run_rl_c1_client_host_lifecycle_probe (const client_options_t &options)
{
    auto consumer = zlink::http_client::client_t::create ()
                      .base_url (options.http_consumer_endpoint)
                      .timeout (std::chrono::milliseconds (10000))
                      .build ();

    for (int index = 0; index < 12; ++index) {
        const auto marker = "rl-c1-" + std::to_string (index);
        const auto reply = consumer.post ("/profile/request/new-client")
                             .body (profile_req_t{.value = "fast", .marker = marker})
                             .fetch<profile_res_t> ();
        ensure (reply.value == "profile:fast", "RL-C1 request failed before cleanup");
    }

    const auto follow_up = consumer.post ("/profile/request/new-client")
                             .body (profile_req_t{.value = "fast",
                                                  .marker = "rl-c1-after-cleanup"})
                             .fetch<profile_res_t> ();
    ensure (follow_up.value == "profile:fast", "RL-C1 follow-up failed after cleanup");

    wait_provider_evidence_prefix (options, "rl-c1-");
    wait_provider_evidence_prefix (options, "rl-c1-after-cleanup");

    std::cout << "scenario RL-C1 passed\n";
}

} // namespace zlink::framework::e2e::resilience_lifecycle::client
