/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <cstdlib>
#include <string>

namespace zlink::framework::e2e::discovery_registry_ha::provider
{

inline std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

struct provider_options_t
{
    std::string rid;
    std::string channel_endpoint;
    std::string http_endpoint;
    std::string registry_router_endpoint;
    std::string log_dir;
    std::string log_name;
};

inline provider_options_t read_provider_options ()
{
    return {.rid = env_or ("ZLINK_CPP_DRHA_RID", "api-a"),
            .channel_endpoint = env_or ("ZLINK_CPP_DRHA_CHANNEL_ENDPOINT"),
            .http_endpoint = env_or ("ZLINK_CPP_DRHA_HTTP_ENDPOINT"),
            .registry_router_endpoint = env_or ("ZLINK_CPP_DRHA_REGISTRY_ROUTER"),
            .log_dir = env_or ("ZLINK_CPP_DRHA_LOG_DIR", "logs"),
            .log_name = env_or ("ZLINK_CPP_DRHA_LOG_NAME", env_or ("ZLINK_CPP_DRHA_RID", "api-a"))};
}

} // namespace zlink::framework::e2e::discovery_registry_ha::provider
