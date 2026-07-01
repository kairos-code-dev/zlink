/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include <cstdlib>
#include <string>

namespace zlink::framework::e2e::runtime_monitoring::registry
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
    std::string rid = env_or ("ZLINK_CPP_E2E_RID", "registry");
    std::string http_endpoint = env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    std::string pub_endpoint = env_or ("ZLINK_CPP_E2E_REGISTRY_PUB");
    std::string router_endpoint = env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER");
    std::string evidence_file = env_or ("ZLINK_CPP_E2E_EVIDENCE_FILE");
    std::string log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR");
};

inline registry_options_t read_registry_options () { return {}; }

} // namespace zlink::framework::e2e::runtime_monitoring::registry
