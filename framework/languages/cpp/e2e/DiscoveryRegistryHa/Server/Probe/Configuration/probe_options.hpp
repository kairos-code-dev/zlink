/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <cstdlib>
#include <string>

namespace zlink::framework::e2e::discovery_registry_ha::probe
{

inline std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

struct probe_options_t
{
    std::string http_endpoint;
    std::string registry_router_endpoint;
    std::string log_dir;
};

inline probe_options_t read_probe_options ()
{
    return {.http_endpoint = env_or ("ZLINK_CPP_DRHA_HTTP_ENDPOINT"),
            .registry_router_endpoint = env_or ("ZLINK_CPP_DRHA_REGISTRY_ROUTER"),
            .log_dir = env_or ("ZLINK_CPP_DRHA_LOG_DIR", "logs")};
}

} // namespace zlink::framework::e2e::discovery_registry_ha::probe
