/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework.hpp>

#include <nlohmann/json.hpp>

#include <string>

namespace zlink::framework::e2e::resilience_lifecycle::registry
{

struct topology_entry_result_t
{
    std::string name;
    std::string kind;
    std::string role;
    std::string routing_id;
    std::string endpoint;
    std::string state;
};

inline topology_entry_result_t to_topology_entry_result (
  const zlink::framework::topology_entry_t &entry)
{
    return {.name = entry.name,
            .kind = entry.kind == zlink::framework::service_kind_t::channel ? "channel"
                                                                            : "other",
            .role = entry.role == zlink::framework::service_role_t::server ? "server" : "other",
            .routing_id = entry.routing_id ? entry.routing_id->to_string () : "",
            .endpoint = entry.endpoint,
            .state = entry.state == zlink::framework::topology_state_t::active ? "Ready"
                                                                               : "other"};
}

inline void to_json (nlohmann::json &json, const topology_entry_result_t &value)
{
    json = nlohmann::json{{"name", value.name},
                          {"kind", value.kind},
                          {"role", value.role},
                          {"routing_id", value.routing_id},
                          {"endpoint", value.endpoint},
                          {"state", value.state}};
}

} // namespace zlink::framework::e2e::resilience_lifecycle::registry
