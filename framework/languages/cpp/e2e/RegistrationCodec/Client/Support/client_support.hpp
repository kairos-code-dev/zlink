/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../Shared/registration_codec_contracts.hpp"

#include <zlink/http_client.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <thread>

namespace zlink::framework::e2e::registration_codec::client
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

inline evidence_snapshot_t fetch_evidence (const std::string &base_url)
{
    auto client = zlink::http_client::client_t::create ()
                    .base_url (base_url)
                    .timeout (std::chrono::milliseconds (1000))
                    .build ();
    return client.get ("/evidence").fetch<evidence_snapshot_t> ();
}

inline bool snapshot_contains (const evidence_snapshot_t &snapshot,
                               const std::string &marker,
                               const std::string &value)
{
    return std::any_of (snapshot.entries.begin (), snapshot.entries.end (),
                        [&] (const evidence_entry_t &entry) {
                            return entry.marker == marker && entry.value == value;
                        });
}

inline evidence_snapshot_t wait_evidence_contains (const std::string &base_url,
                                                   const std::string &marker,
                                                   const std::string &value,
                                                   std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now () + timeout;
    while (std::chrono::steady_clock::now () < deadline) {
        auto snapshot = fetch_evidence (base_url);
        if (std::any_of (snapshot.entries.begin (), snapshot.entries.end (),
                         [&] (const evidence_entry_t &entry) {
                             return entry.marker == marker && entry.value == value;
                         })) {
            return snapshot;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
    throw std::runtime_error ("timed out waiting for evidence " + marker + "=" + value);
}

template <typename TReply>
inline TReply post_empty (const std::string &base_url,
                          const std::string &path,
                          std::chrono::milliseconds timeout = std::chrono::seconds (30))
{
    auto client =
      zlink::http_client::client_t::create ().base_url (base_url).timeout (timeout).build ();
    return client.post (path).template fetch<TReply> ();
}

} // namespace zlink::framework::e2e::registration_codec::client
