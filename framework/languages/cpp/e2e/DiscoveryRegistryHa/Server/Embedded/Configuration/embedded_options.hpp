/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace zlink::framework::e2e::discovery_registry_ha::embedded
{

inline std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

struct embedded_options_t
{
    std::string rid;
    std::string http_endpoint;
    std::string registry_pub_endpoint;
    std::string registry_router_endpoint;
    std::vector<std::string> peer_pub_endpoints;
    std::vector<std::string> discovery_endpoints;
    std::string channel_endpoint;
    std::string log_dir;
    std::string log_name;
};

inline embedded_options_t read_embedded_options ()
{
    std::vector<std::string> peers;
    for (int index = 1;; ++index) {
        auto endpoint = env_or (("ZLINK_CPP_DRHA_REGISTRY_PEER_" + std::to_string (index)).c_str ());
        if (endpoint.empty ()) {
            break;
        }
        peers.push_back (std::move (endpoint));
    }
    std::vector<std::string> discovery;
    if (auto endpoint = env_or ("ZLINK_CPP_DRHA_DISCOVERY_ROUTER"); !endpoint.empty ()) {
        discovery.push_back (std::move (endpoint));
    }
    for (int index = 1;; ++index) {
        auto endpoint =
          env_or (("ZLINK_CPP_DRHA_DISCOVERY_ROUTER_" + std::to_string (index)).c_str ());
        if (endpoint.empty ()) {
            break;
        }
        discovery.push_back (std::move (endpoint));
    }

    auto registry_router = env_or ("ZLINK_CPP_DRHA_REGISTRY_ROUTER");
    if (discovery.empty () && !registry_router.empty ()) {
        discovery.push_back (registry_router);
    }

    return {.rid = env_or ("ZLINK_CPP_DRHA_RID", "embedded-api"),
            .http_endpoint = env_or ("ZLINK_CPP_DRHA_HTTP_ENDPOINT"),
            .registry_pub_endpoint = env_or ("ZLINK_CPP_DRHA_REGISTRY_PUB"),
            .registry_router_endpoint = std::move (registry_router),
            .peer_pub_endpoints = std::move (peers),
            .discovery_endpoints = std::move (discovery),
            .channel_endpoint = env_or ("ZLINK_CPP_DRHA_CHANNEL_ENDPOINT"),
            .log_dir = env_or ("ZLINK_CPP_DRHA_LOG_DIR", "logs"),
            .log_name = env_or ("ZLINK_CPP_DRHA_LOG_NAME", env_or ("ZLINK_CPP_DRHA_RID", "embedded-api"))};
}

} // namespace zlink::framework::e2e::discovery_registry_ha::embedded
