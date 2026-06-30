/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <cstdlib>
#include <string>

namespace zlink::framework::e2e::registry_messaging::registry
{

inline std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

struct registry_options_t
{
    std::string rid;
    std::string pub_endpoint;
    std::string router_endpoint;
    std::string channel_endpoint;
    std::string http_endpoint;
    std::string log_dir;
    std::string evidence_file;
};

inline registry_options_t read_registry_options ()
{
    return {.rid = env_or ("ZLINK_E2E_RID", "registry"),
            .pub_endpoint = env_or ("ZLINK_CPP_E2E_REGISTRY_PUB"),
            .router_endpoint = env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER"),
            .channel_endpoint = env_or ("ZLINK_CPP_E2E_CHANNEL_ENDPOINT"),
            .http_endpoint = env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT"),
            .log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs"),
            .evidence_file = env_or ("ZLINK_CPP_E2E_EVIDENCE_FILE")};
}

} // namespace zlink::framework::e2e::registry_messaging::registry
