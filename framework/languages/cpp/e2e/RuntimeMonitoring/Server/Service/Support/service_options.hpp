/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include <cstdlib>
#include <string>

namespace zlink::framework::e2e::runtime_monitoring::service
{

inline std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

struct service_options_t
{
    std::string rid = env_or ("ZLINK_CPP_E2E_RID", "svc-a");
    std::string http_endpoint = env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    std::string registry_router = env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER");
    std::string channel_endpoint = env_or ("ZLINK_CPP_E2E_CHANNEL_ENDPOINT");
    std::string spot_endpoint = env_or ("ZLINK_CPP_E2E_SPOT_ENDPOINT");
    std::string evidence_file = env_or ("ZLINK_CPP_E2E_EVIDENCE_FILE");
    std::string monitor_profile = env_or ("ZLINK_CPP_E2E_MONITOR_PROFILE", "all");
    std::string log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR");
};

inline service_options_t read_service_options () { return {}; }

} // namespace zlink::framework::e2e::runtime_monitoring::service
