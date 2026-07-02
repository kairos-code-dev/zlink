/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace zlink::framework::e2e::discovery_registry_ha::consumer
{

inline std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

struct consumer_options_t
{
    std::string rid;
    std::string http_endpoint;
    std::vector<std::string> registry_router_endpoints;
    std::string log_dir;
};

inline consumer_options_t read_consumer_options ()
{
    std::vector<std::string> registry_router_endpoints;
    if (auto endpoint = env_or ("ZLINK_CPP_DRHA_REGISTRY_ROUTER"); !endpoint.empty ()) {
        registry_router_endpoints.push_back (std::move (endpoint));
    }
    for (int index = 1;; ++index) {
        auto endpoint = env_or (("ZLINK_CPP_DRHA_REGISTRY_ROUTER_" + std::to_string (index)).c_str ());
        if (endpoint.empty ()) {
            break;
        }
        registry_router_endpoints.push_back (std::move (endpoint));
    }
    return {.rid = env_or ("ZLINK_CPP_DRHA_RID", "consumer-a1"),
            .http_endpoint = env_or ("ZLINK_CPP_DRHA_HTTP_ENDPOINT"),
            .registry_router_endpoints = std::move (registry_router_endpoints),
            .log_dir = env_or ("ZLINK_CPP_DRHA_LOG_DIR", "logs")};
}

} // namespace zlink::framework::e2e::discovery_registry_ha::consumer
