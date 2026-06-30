/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../../Shared/runtime_monitoring_contracts.hpp"

#include <zlink/http_client.hpp>

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <cstdlib>

namespace zlink::framework::e2e::runtime_monitoring::client
{

inline void ensure (bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error (message);
    }
}

inline bool contains (const std::string &value, const std::string &needle)
{
    return value.find (needle) != std::string::npos;
}

inline std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

inline bool any_contains (const std::vector<std::string> &entries, const std::string &needle)
{
    for (const auto &entry : entries) {
        if (contains (entry, needle)) {
            return true;
        }
    }
    return false;
}

inline int count_contains (const std::vector<std::string> &entries, const std::string &needle)
{
    int count = 0;
    for (const auto &entry : entries) {
        if (contains (entry, needle)) {
            ++count;
        }
    }
    return count;
}

inline std::vector<std::string> fetch_evidence (const std::string &base_url)
{
    auto http = zlink::http_client::client_t::create ()
                  .base_url (base_url)
                  .timeout (std::chrono::milliseconds (1000))
                  .build ();
    return http.get ("/evidence").fetch<std::vector<std::string>> ();
}

inline std::vector<std::string> wait_evidence_contains (const std::string &base_url,
                                                        const std::string &required,
                                                        std::chrono::milliseconds timeout)
{
    auto http = zlink::http_client::client_t::create ()
                  .base_url (base_url)
                  .timeout (timeout + std::chrono::milliseconds (1000))
                  .build ();
    return http.post ("/evidence/wait")
      .body (evidence_wait_req_t{.contains_all = {required},
                                     .contains_any_groups = {},
                                     .timeout_milliseconds = static_cast<int> (timeout.count ())})
      .fetch<std::vector<std::string>> ();
}

inline std::vector<std::string> wait_evidence_count_at_least (
  const std::string &base_url,
  const std::string &required,
  int expected_count,
  std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now () + timeout;
    while (std::chrono::steady_clock::now () < deadline) {
        auto entries = fetch_evidence (base_url);
        if (count_contains (entries, required) >= expected_count) {
            return entries;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
    throw std::runtime_error ("timed out waiting for evidence count: " + required);
}

inline profile_res_t post_profile_request (const std::string &base_url,
                                             const std::string &path,
                                             const profile_req_t &request)
{
    auto http = zlink::http_client::client_t::create ()
                  .base_url (base_url)
                  .timeout (std::chrono::milliseconds (5000))
                  .build ();
    return http.post (path).body (request).fetch<profile_res_t> ();
}

inline std::vector<std::string> wait_log_contains (const std::string &base_url,
                                                   const std::string &path,
                                                   const std::string &required,
                                                   std::chrono::milliseconds timeout)
{
    auto http = zlink::http_client::client_t::create ()
                  .base_url (base_url)
                  .timeout (timeout + std::chrono::milliseconds (1000))
                  .build ();
    return http.post (path)
      .body (evidence_wait_req_t{.contains_all = {required},
                                     .contains_any_groups = {},
                                     .timeout_milliseconds = static_cast<int> (timeout.count ())})
      .fetch<std::vector<std::string>> ();
}

} // namespace zlink::framework::e2e::runtime_monitoring::client
