/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../../Shared/registry_messaging_contracts.hpp"

#include <zlink/framework.hpp>
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
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (std::filesystem::exists (path)) {
            return;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (50));
    }
    throw std::runtime_error ("timed out waiting for " + path);
}

inline void configure_common_codecs (zlink::framework::codec_options_builder_t codecs)
{
    codecs.add_json ();
    codecs.add_json<profile_request_t,
                    profile_reply_t,
                    profile_command_t,
                    payload_request_t,
                    payload_reply_t,
                    workflow_request_t,
                    workflow_reply_t,
                    route_ping_t,
                    route_pong_t> ();
}

inline evidence_snapshot_t fetch_evidence (const std::string &base_url)
{
    auto client = zlink::http_client::client_t::create ()
                    .base_url (base_url)
                    .json ()
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

} // namespace zlink::framework::e2e::registry_messaging::client
