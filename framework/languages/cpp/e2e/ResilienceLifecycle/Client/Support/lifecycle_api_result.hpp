/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/resilience_lifecycle_contracts.hpp"
#include "scenario_assert.hpp"

#include <zlink/framework.hpp>
#include <zlink/http_client.hpp>

#include <chrono>
#include <string>

namespace zlink::framework::e2e::resilience_lifecycle::client
{

inline void configure_common_codecs (zlink::framework::codec_options_builder_t)
{
}

inline evidence_snapshot_t fetch_evidence (const std::string &base_url)
{
    auto client = zlink::http_client::client_t::create ()
                    .base_url (base_url)
                    .timeout (std::chrono::milliseconds (1000))
                    .build ();
    return client.get ("/evidence").fetch<evidence_snapshot_t> ();
}

inline void post_provider_admin (const std::string &base_url, const std::string &path)
{
    auto http = zlink::http_client::client_t::create ()
                  .base_url (base_url)
                  .timeout (std::chrono::milliseconds (1000))
                  .build ();
    auto response = http.post (path).submit_raw ().result ();
    ensure (response && response.value ().status < 400,
            "provider admin call failed: " + base_url + path);
}

inline bool evidence_contains (const evidence_snapshot_t &snapshot,
                               const std::string &marker,
                               const std::string &value)
{
    for (const auto &entry : snapshot.entries) {
        if (entry.marker == marker && entry.value == value) {
            return true;
        }
    }
    return false;
}

inline bool any_provider_evidence_contains (const std::string &marker, const std::string &value)
{
    return evidence_contains (fetch_evidence (env_or ("ZLINK_CPP_E2E_HTTP_A_ENDPOINT")), marker,
                              value)
           || evidence_contains (fetch_evidence (env_or ("ZLINK_CPP_E2E_HTTP_B_ENDPOINT")),
                                 marker, value);
}

inline void wait_provider_evidence_contains (const std::string &marker,
                                             const std::string &value,
                                             std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now () + timeout;
    while (std::chrono::steady_clock::now () < deadline) {
        if (any_provider_evidence_contains (marker, value)) {
            return;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
    throw std::runtime_error ("timed out waiting for provider evidence " + marker + "=" + value);
}

} // namespace zlink::framework::e2e::resilience_lifecycle::client
