/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../../Shared/registry_messaging_contracts.hpp"

#include <zlink/http_client.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace zlink::framework::e2e::registry_messaging::client
{

inline std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

inline void ensure (bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error (message);
    }
}

inline void touch_file (const std::string &path)
{
    if (path.empty ()) {
        return;
    }
    std::ofstream file (path);
    file << "ready\n";
}

inline void wait_for_file (const std::string &path)
{
    if (path.empty ()) {
        return;
    }
    const auto timeout_ms =
      std::chrono::milliseconds (std::stoi (env_or ("ZLINK_CPP_E2E_CONTROL_WAIT_MS", "60000")));
    const auto deadline = std::chrono::steady_clock::now () + timeout_ms;
    do {
        if (std::filesystem::exists (path)) {
            return;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (50));
    } while (std::chrono::steady_clock::now () < deadline);
    throw std::runtime_error ("timed out waiting for " + path);
}

inline evidence_snapshot_t fetch_evidence (const std::string &base_url)
{
    auto client = zlink::http_client::client_t::create ()
                    .base_url (base_url)
                    .timeout (std::chrono::milliseconds (1000))
                    .build ();
    return client.get ("/evidence").fetch<evidence_snapshot_t> ();
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

inline int evidence_value_prefix_count (const evidence_snapshot_t &snapshot,
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

template <typename TRequest, typename TReply>
inline TReply post_json (const std::string &base_url,
                         const std::string &path,
                         const TRequest &request,
                         std::chrono::milliseconds timeout = std::chrono::seconds (30))
{
    auto client = zlink::http_client::client_t::create ()
                    .base_url (base_url)
                    .timeout (timeout)
                    .build ();
    return client.post (path).body (request).template fetch<TReply> ();
}

inline void post_raw (const std::string &base_url,
                      const std::string &path,
                      std::chrono::milliseconds timeout = std::chrono::seconds (30))
{
    auto client = zlink::http_client::client_t::create ()
                    .base_url (base_url)
                    .timeout (timeout)
                    .build ();
    auto response = client.post (path).submit_raw ().result ();
    ensure (response && response.value ().status < 400, "HTTP POST failed: " + path);
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

inline evidence_snapshot_t wait_evidence_contains (const std::string &base_url,
                                                   const std::string &marker,
                                                   const std::string &value,
                                                   std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now () + timeout;
    while (std::chrono::steady_clock::now () < deadline) {
        auto snapshot = fetch_evidence (base_url);
        if (evidence_contains (snapshot, marker, value)) {
            return snapshot;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
    throw std::runtime_error ("timed out waiting for evidence " + marker + "=" + value);
}

} // namespace zlink::framework::e2e::registry_messaging::client
