/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace zlink::framework::e2e::discovery_registry_ha
{

inline constexpr const char *api_channel = "discovery.registry.ha.profile";
inline constexpr const char *handler_group = "discovery-registry-ha";

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

struct evidence_wait_req_t
{
    std::string contains;
    int timeout_milliseconds = 10000;
};

struct topology_ready_wait_req_t
{
    int ready_count = 0;
    int timeout_milliseconds = 10000;
};

struct member_endpoint_wait_req_t
{
    std::string endpoint;
    int timeout_milliseconds = 10000;
};

struct registry_peer_count_wait_req_t
{
    int connected_peer_count = 0;
    int timeout_milliseconds = 10000;
};

struct operation_status_t
{
    std::string status;
};

struct evidence_entry_t
{
    std::string marker;
    std::string provider_rid;
    std::string value;
};

struct evidence_snapshot_t
{
    std::string provider_rid;
    std::vector<evidence_entry_t> entries;
};

struct topology_snapshot_t
{
    std::vector<std::string> entries;
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

inline void to_json (nlohmann::json &json, const evidence_wait_req_t &value)
{
    json = nlohmann::json{{"contains", value.contains},
                          {"timeout_milliseconds", value.timeout_milliseconds}};
}

inline void from_json (const nlohmann::json &json, evidence_wait_req_t &value)
{
    json.at ("contains").get_to (value.contains);
    if (json.contains ("timeout_milliseconds")) {
        json.at ("timeout_milliseconds").get_to (value.timeout_milliseconds);
    }
}

inline void to_json (nlohmann::json &json, const topology_ready_wait_req_t &value)
{
    json = nlohmann::json{{"ready_count", value.ready_count},
                          {"timeout_milliseconds", value.timeout_milliseconds}};
}

inline void from_json (const nlohmann::json &json, topology_ready_wait_req_t &value)
{
    json.at ("ready_count").get_to (value.ready_count);
    if (json.contains ("timeout_milliseconds")) {
        json.at ("timeout_milliseconds").get_to (value.timeout_milliseconds);
    }
}

inline void to_json (nlohmann::json &json, const member_endpoint_wait_req_t &value)
{
    json = nlohmann::json{{"endpoint", value.endpoint},
                          {"timeout_milliseconds", value.timeout_milliseconds}};
}

inline void from_json (const nlohmann::json &json, member_endpoint_wait_req_t &value)
{
    json.at ("endpoint").get_to (value.endpoint);
    if (json.contains ("timeout_milliseconds")) {
        json.at ("timeout_milliseconds").get_to (value.timeout_milliseconds);
    }
}

inline void to_json (nlohmann::json &json, const registry_peer_count_wait_req_t &value)
{
    json = nlohmann::json{{"connected_peer_count", value.connected_peer_count},
                          {"timeout_milliseconds", value.timeout_milliseconds}};
}

inline void from_json (const nlohmann::json &json, registry_peer_count_wait_req_t &value)
{
    json.at ("connected_peer_count").get_to (value.connected_peer_count);
    if (json.contains ("timeout_milliseconds")) {
        json.at ("timeout_milliseconds").get_to (value.timeout_milliseconds);
    }
}

inline void to_json (nlohmann::json &json, const operation_status_t &value)
{
    json = nlohmann::json{{"status", value.status}};
}

inline void from_json (const nlohmann::json &json, operation_status_t &value)
{
    json.at ("status").get_to (value.status);
}

inline void to_json (nlohmann::json &json, const evidence_entry_t &value)
{
    json = nlohmann::json{
      {"marker", value.marker}, {"provider_rid", value.provider_rid}, {"value", value.value}};
}

inline void from_json (const nlohmann::json &json, evidence_entry_t &value)
{
    json.at ("marker").get_to (value.marker);
    json.at ("provider_rid").get_to (value.provider_rid);
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const evidence_snapshot_t &value)
{
    json = nlohmann::json{{"provider_rid", value.provider_rid}, {"entries", value.entries}};
}

inline void from_json (const nlohmann::json &json, evidence_snapshot_t &value)
{
    json.at ("provider_rid").get_to (value.provider_rid);
    json.at ("entries").get_to (value.entries);
}

inline void to_json (nlohmann::json &json, const topology_snapshot_t &value)
{
    json = nlohmann::json{{"entries", value.entries}};
}

inline void from_json (const nlohmann::json &json, topology_snapshot_t &value)
{
    json.at ("entries").get_to (value.entries);
}

} // namespace zlink::framework::e2e::discovery_registry_ha
