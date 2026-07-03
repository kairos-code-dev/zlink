/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <cstdlib>
#include <string>

namespace zlink::framework::e2e::registry_messaging::workflow
{

inline std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

struct workflow_options_t
{
    std::string rid;
    std::string instance_id;
    std::string workflow_endpoint;
    std::string http_endpoint;
    std::string redis_endpoint;
    std::string redis_key_prefix;
    std::string log_dir;
};

inline workflow_options_t read_workflow_options ()
{
    auto rid = env_or ("ZLINK_CPP_E2E_PROVIDER_RID", "workflow-a");
    return {.rid = rid,
            .instance_id = env_or ("ZLINK_CPP_E2E_PROVIDER_INSTANCE", rid),
            .workflow_endpoint = env_or ("ZLINK_CPP_E2E_WORKFLOW_ENDPOINT"),
            .http_endpoint = env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT"),
            .redis_endpoint = env_or ("ZLINK_CPP_E2E_REDIS_ENDPOINT"),
            .redis_key_prefix = env_or ("ZLINK_CPP_E2E_REDIS_KEY_PREFIX"),
            .log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs")};
}

} // namespace zlink::framework::e2e::registry_messaging::workflow
