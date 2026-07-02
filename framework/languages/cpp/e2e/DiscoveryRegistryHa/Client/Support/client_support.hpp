/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/discovery_registry_ha_contracts.hpp"

#include <zlink/http_client.hpp>

#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace zlink::framework::e2e::discovery_registry_ha::client
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

template <typename TRequest, typename TReply>
inline TReply post_json (const std::string &base_url,
                         const std::string &path,
                         const TRequest &request,
                         std::chrono::milliseconds timeout = std::chrono::seconds (10))
{
    auto client =
      zlink::http_client::client_t::create ().base_url (base_url).timeout (timeout).build ();
    return client.post (path).body (request).template fetch<TReply> ();
}

template <typename TReply>
inline TReply get_json (const std::string &base_url,
                        const std::string &path,
                        std::chrono::milliseconds timeout = std::chrono::seconds (10))
{
    auto client =
      zlink::http_client::client_t::create ().base_url (base_url).timeout (timeout).build ();
    return client.get (path).template fetch<TReply> ();
}

inline bool evidence_contains (const evidence_snapshot_t &snapshot,
                               const std::string &marker,
                               const std::string &provider_rid)
{
    for (const auto &entry : snapshot.entries) {
        if (entry.marker == marker && entry.provider_rid == provider_rid
            && entry.value.find ("rid=" + provider_rid) != std::string::npos) {
            return true;
        }
    }
    return false;
}

inline std::string unique_marker (const std::string &prefix)
{
    return prefix + "-"
           + std::to_string (std::chrono::steady_clock::now ().time_since_epoch ().count ());
}

inline void wait_member_endpoint (const std::string &registry_url, const std::string &endpoint)
{
    post_json<member_endpoint_wait_req_t, operation_status_t> (
      registry_url, "/registry/members/wait", {.endpoint = endpoint});
}

inline void verify_profile_request (const std::string &scenario,
                                    const std::string &name,
                                    const std::string &consumer_url,
                                    const std::string &provider_a_url,
                                    const std::string &provider_b_url,
                                    const std::string &marker_prefix)
{
    const auto marker = unique_marker (marker_prefix + "-" + name);
    const auto reply = post_json<profile_req_t, profile_res_t> (
      consumer_url, "/profile/request", {.value = scenario, .marker = marker});
    ensure (reply.value == "profile:" + scenario, scenario + " " + name + " request failed");
    ensure (reply.provider_rid == "api-a" || reply.provider_rid == "api-b",
            scenario + " " + name + " routed to an unexpected provider");
    ensure (reply.marker == marker, scenario + " " + name + " marker mismatch");

    const auto evidence_url = reply.provider_rid == "api-a" ? provider_a_url : provider_b_url;
    const auto evidence = post_json<evidence_wait_req_t, evidence_snapshot_t> (
      evidence_url, "/evidence/wait", {.contains = marker});
    ensure (evidence_contains (evidence, marker, reply.provider_rid),
            scenario + " " + name + " provider evidence was not recorded");
}

inline void verify_profile_request_from_provider (const std::string &scenario,
                                                  const std::string &name,
                                                  const std::string &consumer_url,
                                                  const std::string &provider_url,
                                                  const std::string &expected_provider_rid,
                                                  const std::string &marker_prefix)
{
    const auto marker = unique_marker (marker_prefix + "-" + name);
    const auto reply = post_json<profile_req_t, profile_res_t> (
      consumer_url, "/profile/request", {.value = scenario, .marker = marker});
    ensure (reply.value == "profile:" + scenario, scenario + " " + name + " request failed");
    ensure (reply.provider_rid == expected_provider_rid,
            scenario + " " + name + " routed to " + reply.provider_rid + " instead of "
              + expected_provider_rid);
    ensure (reply.marker == marker, scenario + " " + name + " marker mismatch");

    const auto evidence = post_json<evidence_wait_req_t, evidence_snapshot_t> (
      provider_url, "/evidence/wait", {.contains = marker});
    ensure (evidence_contains (evidence, marker, expected_provider_rid),
            scenario + " " + name + " provider evidence was not recorded");
}

inline void verify_profile_request_allowing_embedded (const std::string &scenario,
                                                      const std::string &name,
                                                      const std::string &consumer_url,
                                                      const std::string &provider_a_url,
                                                      const std::string &provider_b_url,
                                                      const std::string &embedded_url,
                                                      const std::string &embedded_rid,
                                                      const std::string &marker_prefix)
{
    const auto marker = unique_marker (marker_prefix + "-" + name);
    const auto reply = post_json<profile_req_t, profile_res_t> (
      consumer_url, "/profile/request", {.value = scenario, .marker = marker});
    ensure (reply.value == "profile:" + scenario, scenario + " " + name + " request failed");
    ensure (reply.provider_rid == "api-a" || reply.provider_rid == "api-b"
              || reply.provider_rid == embedded_rid,
            scenario + " " + name + " routed to an unexpected provider");
    ensure (reply.marker == marker, scenario + " " + name + " marker mismatch");

    const auto evidence_url = reply.provider_rid == "api-a"      ? provider_a_url
                              : reply.provider_rid == "api-b"    ? provider_b_url
                                                                  : embedded_url;
    const auto evidence = post_json<evidence_wait_req_t, evidence_snapshot_t> (
      evidence_url, "/evidence/wait", {.contains = marker});
    ensure (evidence_contains (evidence, marker, reply.provider_rid),
            scenario + " " + name + " provider evidence was not recorded");
}

inline bool health_probe_fails (const std::string &base_url,
                                std::chrono::milliseconds timeout = std::chrono::milliseconds (500))
{
    auto client = zlink::http_client::client_t::create ().base_url (base_url).timeout (timeout).build ();
    try {
        const auto result = client.get ("/health").submit_raw ().result ();
        return !static_cast<bool> (result);
    } catch (...) {
        return true;
    }
}

} // namespace zlink::framework::e2e::discovery_registry_ha::client
