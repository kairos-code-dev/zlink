/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>
#include <vector>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rm_c8_payload_round_trip_scenario ()
{
    const auto consumer = env_or ("ZLINK_CPP_E2E_SINGLE_CONSUMER_URL");
    const std::vector<std::size_t> sizes = {1, 4096, 256 * 1024, 1024 * 1024};
    for (const auto size : sizes) {
        const auto payload = std::string (size, static_cast<char> ('a' + (size % 23)));
        const auto marker = "payload-" + std::to_string (size);
        auto reply = post_json<payload_req_t, payload_res_t> (
          consumer, "/profile/payload", payload_req_t{.marker = marker, .payload = payload},
          std::chrono::seconds (10));
        ensure (reply.marker == marker,
                "RM-C8 reply marker mismatch for payload size " + std::to_string (size));
        ensure (reply.length == static_cast<int> (payload.size ()),
                "RM-C8 reply length mismatch for payload size " + std::to_string (size));
        ensure (reply.sha256 == sha256_hex (payload),
                "RM-C8 reply sha256 mismatch for payload size " + std::to_string (size));
    }
    auto follow_up = post_json<profile_req_t, profile_res_t> (
      consumer, "/profile/request", profile_req_t{.value = "rm-c8-after"});
    ensure (follow_up.value == "profile:rm-c8-after", "RM-C8 follow-up request failed");
    std::cout << "scenario RM-C8 passed\n";
}

inline void run_rm_c8_max_message_size_scenario ()
{
    const auto consumer = env_or ("ZLINK_CPP_E2E_SINGLE_CONSUMER_URL");
    const auto oversized_payload = std::string (32 * 1024, 'x');
    bool oversized_failed = false;
    try {
        (void) post_json<payload_req_t, payload_res_t> (
          consumer, "/profile/payload",
          payload_req_t{.marker = "oversized", .payload = oversized_payload},
          std::chrono::milliseconds (1500));
    }
    catch (const std::exception &) {
        oversized_failed = true;
    }
    ensure (oversized_failed, "RM-C8 oversized request unexpectedly succeeded");

    auto normal = post_json<payload_req_t, payload_res_t> (
      consumer, "/profile/payload",
      payload_req_t{.marker = "after-oversized", .payload = "after-oversized"});
    ensure (normal.marker == "after-oversized", "RM-C8 normal marker after oversized mismatch");
    ensure (normal.sha256 == sha256_hex ("after-oversized"),
            "RM-C8 normal sha256 after oversized mismatch");
    std::cout << "scenario RM-C8-max passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
