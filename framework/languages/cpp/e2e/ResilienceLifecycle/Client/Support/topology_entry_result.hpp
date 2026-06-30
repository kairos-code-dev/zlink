/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace zlink::framework::e2e::resilience_lifecycle::client
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

inline void from_json (const nlohmann::json &json, topology_entry_result_t &value)
{
    json.at ("name").get_to (value.name);
    json.at ("kind").get_to (value.kind);
    json.at ("role").get_to (value.role);
    value.routing_id = json.value ("routing_id", "");
    json.at ("endpoint").get_to (value.endpoint);
    json.at ("state").get_to (value.state);
}

} // namespace zlink::framework::e2e::resilience_lifecycle::client
