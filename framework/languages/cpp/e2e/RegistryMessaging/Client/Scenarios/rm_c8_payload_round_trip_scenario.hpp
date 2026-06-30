/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>
#include <vector>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rm_c8_payload_round_trip_scenario (zlink::framework::channel_client_t &channels)
{
    (void) channels;
    const auto consumer = env_or ("ZLINK_CPP_E2E_SINGLE_CONSUMER_URL");
    const std::vector<std::size_t> sizes = {1, 4096, 256 * 1024, 1024 * 1024};
    for (const auto size : sizes) {
        const auto payload = std::string (size, static_cast<char> ('a' + (size % 23)));
        const auto marker = "payload-" + std::to_string (size);
        auto reply = post_json<payload_request_t, payload_reply_t> (
          consumer, "/profile/payload", payload_request_t{.marker = marker, .payload = payload},
          std::chrono::seconds (10));
        ensure (reply.marker == marker,
                "RM-C8 reply marker mismatch for payload size " + std::to_string (size));
        ensure (reply.length == static_cast<int> (payload.size ()),
                "RM-C8 reply length mismatch for payload size " + std::to_string (size));
        ensure (reply.sha256 == sha256_hex (payload),
                "RM-C8 reply sha256 mismatch for payload size " + std::to_string (size));
    }
    auto follow_up = post_json<profile_request_t, profile_reply_t> (
      consumer, "/profile/request", profile_request_t{.value = "rm-c8-after"});
    ensure (follow_up.value == "profile:rm-c8-after", "RM-C8 follow-up request failed");
    std::cout << "scenario RM-C8 passed\n";
}

inline void run_rm_c8_max_message_size_scenario (zlink::framework::channel_client_t &channels)
{
    const auto oversized_payload = std::string (32 * 1024, 'x');
    auto oversized = channels
                       .request ("registry.messaging.api.manual.max",
                      payload_request_t{.marker = "oversized", .payload = oversized_payload})
                       .timeout (std::chrono::milliseconds (1000))
                       .async<payload_reply_t> ();
    ensure (!oversized.result ().has_value (), "RM-C8 oversized request unexpectedly succeeded");
    ensure (oversized.result ().error_kind () == zlink::framework::framework_error_kind_t::timeout,
            "RM-C8 oversized request failed with an unexpected public error kind");

    auto normal = channels
                    .request ("registry.messaging.api.manual.max",
                              payload_request_t{.marker = "after-oversized",
                                                .payload = "after-oversized"})
                    .timeout (std::chrono::milliseconds (2000))
                    .async<payload_reply_t> ();
    ensure (normal.result ().has_value (), "RM-C8 normal request after oversized failed");
    ensure (normal.result ().value ().marker == "after-oversized",
            "RM-C8 normal marker after oversized mismatch");
    ensure (normal.result ().value ().sha256 == sha256_hex ("after-oversized"),
            "RM-C8 normal sha256 after oversized mismatch");
    std::cout << "scenario RM-C8-max passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
