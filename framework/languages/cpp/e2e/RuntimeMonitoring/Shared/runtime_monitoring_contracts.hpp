/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace zlink::framework::e2e::runtime_monitoring
{

inline constexpr const char *profile_channel = "monitor.profile";
inline constexpr const char *channel_server_source = "monitor.profile.server";
inline constexpr const char *channel_client_source = "monitor.profile.client";
inline constexpr const char *spot_channel = "monitor.spot";
inline constexpr const char *spot_node = "monitor.spot";
inline constexpr const char *handler_group = "runtime-monitoring";
inline constexpr const char *route_mesh_name = "runtime.monitoring.mesh";
inline constexpr const char *route_mesh_channel = route_mesh_name;
inline constexpr const char *monitoring_subject_spot = "monitoring-subject";
inline constexpr const char *monitoring_slow_spot = "monitoring-slow";

struct multicast_probe_t
{
    std::string marker;
    int sequence = 0;
    std::string payload;
};

struct multicast_publish_req_t
{
    std::string marker;
    int payload_bytes = 1024 * 1024;
    int max_attempts = 20000;
    bool blocking = false;
    std::optional<std::uint64_t> expected_remote_dropped;
    std::optional<std::uint64_t> expected_local_dropped;
};

struct multicast_publish_res_t
{
    std::string status;
    int sequence = 0;
    std::uint64_t snapshot_remote = 0;
    std::uint64_t admitted_remote = 0;
    std::uint64_t dropped_remote = 0;
    std::uint64_t snapshot_local = 0;
    std::uint64_t admitted_local = 0;
    std::uint64_t dropped_local = 0;
    std::uint64_t submitted_total = 0;
    std::uint64_t backpressured_total = 0;
    std::uint64_t dropped_total = 0;
};

struct profile_req_t
{
    std::string value;
    std::string marker;
};

struct profile_res_t
{
    std::string value;
    std::string provider_rid;
    std::string marker;
};

struct application_gate_req_t
{
    std::string marker;
};

struct application_gate_res_t
{
    std::string marker;
    std::string provider_rid;
};

struct evidence_wait_req_t
{
    std::vector<std::string> contains_all;
    std::vector<std::vector<std::string>> contains_any_groups;
    int timeout_milliseconds = 10000;
};

inline void to_json (nlohmann::json &json, const profile_req_t &value)
{
    json = nlohmann::json{{"value", value.value}, {"marker", value.marker}};
}

inline void from_json (const nlohmann::json &json, profile_req_t &value)
{
    json.at ("value").get_to (value.value);
    json.at ("marker").get_to (value.marker);
}

inline void to_json (nlohmann::json &json, const profile_res_t &value)
{
    json = nlohmann::json{
      {"value", value.value}, {"provider_rid", value.provider_rid}, {"marker", value.marker}};
}

inline void from_json (const nlohmann::json &json, profile_res_t &value)
{
    json.at ("value").get_to (value.value);
    json.at ("provider_rid").get_to (value.provider_rid);
    json.at ("marker").get_to (value.marker);
}

inline void to_json (nlohmann::json &json, const application_gate_req_t &value)
{
    json = nlohmann::json{{"marker", value.marker}};
}

inline void from_json (const nlohmann::json &json, application_gate_req_t &value)
{
    json.at ("marker").get_to (value.marker);
}

inline void to_json (nlohmann::json &json, const application_gate_res_t &value)
{
    json = nlohmann::json{
      {"marker", value.marker}, {"provider_rid", value.provider_rid}};
}

inline void from_json (const nlohmann::json &json, application_gate_res_t &value)
{
    json.at ("marker").get_to (value.marker);
    json.at ("provider_rid").get_to (value.provider_rid);
}

inline void to_json (nlohmann::json &json, const evidence_wait_req_t &value)
{
    json = nlohmann::json{{"contains_all", value.contains_all},
                          {"contains_any_groups", value.contains_any_groups},
                          {"timeout_milliseconds", value.timeout_milliseconds}};
}

inline void from_json (const nlohmann::json &json, evidence_wait_req_t &value)
{
    if (json.contains ("contains_all")) {
        json.at ("contains_all").get_to (value.contains_all);
    }
    if (json.contains ("contains_any_groups")) {
        json.at ("contains_any_groups").get_to (value.contains_any_groups);
    }
    if (json.contains ("timeout_milliseconds")) {
        json.at ("timeout_milliseconds").get_to (value.timeout_milliseconds);
    }
}

inline void to_json (nlohmann::json &json, const multicast_probe_t &value)
{
    json = nlohmann::json{{"marker", value.marker},
                          {"sequence", value.sequence},
                          {"payload", value.payload}};
}

inline void from_json (const nlohmann::json &json, multicast_probe_t &value)
{
    json.at ("marker").get_to (value.marker);
    json.at ("sequence").get_to (value.sequence);
    json.at ("payload").get_to (value.payload);
}

inline void to_json (nlohmann::json &json, const multicast_publish_req_t &value)
{
    json = nlohmann::json{{"marker", value.marker},
                          {"payloadBytes", value.payload_bytes},
                          {"maxAttempts", value.max_attempts},
                          {"blocking", value.blocking}};
    if (value.expected_remote_dropped)
        json["expectedRemoteDropped"] = *value.expected_remote_dropped;
    if (value.expected_local_dropped)
        json["expectedLocalDropped"] = *value.expected_local_dropped;
}

inline void from_json (const nlohmann::json &json, multicast_publish_req_t &value)
{
    json.at ("marker").get_to (value.marker);
    if (json.contains ("payloadBytes"))
        json.at ("payloadBytes").get_to (value.payload_bytes);
    if (json.contains ("maxAttempts"))
        json.at ("maxAttempts").get_to (value.max_attempts);
    if (json.contains ("blocking"))
        json.at ("blocking").get_to (value.blocking);
    if (json.contains ("expectedRemoteDropped"))
        value.expected_remote_dropped =
          json.at ("expectedRemoteDropped").get<std::uint64_t> ();
    if (json.contains ("expectedLocalDropped"))
        value.expected_local_dropped =
          json.at ("expectedLocalDropped").get<std::uint64_t> ();
}

inline void to_json (nlohmann::json &json, const multicast_publish_res_t &value)
{
    json = nlohmann::json{
      {"status", value.status},
      {"sequence", value.sequence},
      {"snapshotRemote", value.snapshot_remote},
      {"admittedRemote", value.admitted_remote},
      {"droppedRemote", value.dropped_remote},
      {"snapshotLocal", value.snapshot_local},
      {"admittedLocal", value.admitted_local},
      {"droppedLocal", value.dropped_local},
      {"submittedTotal", value.submitted_total},
      {"backpressuredTotal", value.backpressured_total},
      {"droppedTotal", value.dropped_total}};
}

inline void from_json (const nlohmann::json &json, multicast_publish_res_t &value)
{
    json.at ("status").get_to (value.status);
    json.at ("sequence").get_to (value.sequence);
    json.at ("snapshotRemote").get_to (value.snapshot_remote);
    json.at ("admittedRemote").get_to (value.admitted_remote);
    json.at ("droppedRemote").get_to (value.dropped_remote);
    json.at ("snapshotLocal").get_to (value.snapshot_local);
    json.at ("admittedLocal").get_to (value.admitted_local);
    json.at ("droppedLocal").get_to (value.dropped_local);
    json.at ("submittedTotal").get_to (value.submitted_total);
    json.at ("backpressuredTotal").get_to (value.backpressured_total);
    json.at ("droppedTotal").get_to (value.dropped_total);
}

} // namespace zlink::framework::e2e::runtime_monitoring
