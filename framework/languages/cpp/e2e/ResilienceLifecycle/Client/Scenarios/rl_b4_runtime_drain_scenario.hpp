/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/resilience_request_support.hpp"

#include <zlink/http_client.hpp>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace zlink::framework::e2e::resilience_lifecycle::client
{

inline int count_evidence_prefix (const evidence_snapshot_t &snapshot,
                                  const std::string &marker,
                                  const std::string &prefix)
{
    int count = 0;
    for (const auto &entry : snapshot.entries) {
        if (entry.marker == marker && entry.value.rfind (prefix, 0) == 0) {
            ++count;
        }
    }
    return count;
}

inline void wait_provider_evidence_prefix_on (const std::string &base_url,
                                              const std::string &prefix)
{
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (15);
    while (std::chrono::steady_clock::now () < deadline) {
        const auto evidence = fetch_evidence (base_url);
        if (count_evidence_prefix (evidence, "ProfileReq", prefix) > 0) {
            return;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
    throw std::runtime_error ("timed out waiting for provider evidence " + prefix);
}

inline void wait_provider_weight_on (zlink::http_client::client_t &provider, int expected)
{
    auto result = provider.post ("/admin/weight/wait")
                    .body (nlohmann::json{{"expected", expected}}.dump (), "application/json")
                    .submit_raw ()
                    .result ();
    if (!result) {
        throw std::runtime_error (result.error () ? result.error ()->what ()
                                                  : "provider weight wait failed");
    }
    if (result.value ().status >= 400) {
        throw std::runtime_error ("provider weight wait failed with status "
                                  + std::to_string (result.value ().status) + ": "
                                  + result.value ().body);
    }
}

inline void post_provider_admin_on (zlink::http_client::client_t &provider,
                                    const std::string &path)
{
    auto result = provider.post (path).submit_raw ().result ();
    if (!result) {
        throw std::runtime_error (result.error () ? result.error ()->what ()
                                                  : "provider admin call failed");
    }
    if (result.value ().status >= 400) {
        throw std::runtime_error ("provider admin call failed with status "
                                  + std::to_string (result.value ().status) + " for " + path
                                  + ": " + result.value ().body);
    }
}

inline void run_rl_b4_runtime_drain_scenario ()
{
    auto consumer = zlink::http_client::client_t::create ()
                      .base_url (env_or ("ZLINK_CPP_E2E_HTTP_CONSUMER_ENDPOINT"))
                      .timeout (std::chrono::milliseconds (10000))
                      .build ();
    auto provider_b = zlink::http_client::client_t::create ()
                        .base_url (env_or ("ZLINK_CPP_E2E_HTTP_B_ENDPOINT"))
                        .timeout (std::chrono::milliseconds (10000))
                        .build ();

    const auto before_drain = fetch_evidence (env_or ("ZLINK_CPP_E2E_HTTP_B_ENDPOINT"));
    post_provider_admin_on (provider_b, "/admin/drain");
    wait_provider_weight_on (provider_b, 0);

    for (int index = 0; index < 20; ++index) {
        const auto marker = "rl-b4-drained-" + std::to_string (index);
        const auto reply = consumer.post ("/profile/request")
                             .body (profile_req_t{.value = "fast", .marker = marker})
                             .fetch<profile_res_t> ();
        ensure (reply.provider_rid == "api-a", "RL-B4 drained provider received request");
    }

    const auto after_drain = fetch_evidence (env_or ("ZLINK_CPP_E2E_HTTP_B_ENDPOINT"));
    const auto new_b =
      count_evidence_prefix (after_drain, "ProfileReq", "rl-b4-drained-")
      - count_evidence_prefix (before_drain, "ProfileReq", "rl-b4-drained-");
    ensure (new_b == 0, "RL-B4 api-b evidence changed after drain");
    wait_provider_evidence_prefix_on (env_or ("ZLINK_CPP_E2E_HTTP_A_ENDPOINT"),
                                      "rl-b4-drained-");

    post_provider_admin_on (provider_b, "/admin/restore");
    wait_provider_weight_on (provider_b, 100);

    for (int index = 0; index < 40; ++index) {
        const auto marker = "rl-b4-restored-" + std::to_string (index);
        const auto reply = consumer.post ("/profile/request")
                             .body (profile_req_t{.value = "fast", .marker = marker})
                             .fetch<profile_res_t> ();
        ensure (reply.value == "profile:fast",
                "RL-B4 restored request returned an unexpected value");
    }
    wait_provider_evidence_prefix_on (env_or ("ZLINK_CPP_E2E_HTTP_B_ENDPOINT"),
                                      "rl-b4-restored-");

    std::cout << "scenario RL-B4 passed\n";
}

} // namespace zlink::framework::e2e::resilience_lifecycle::client
