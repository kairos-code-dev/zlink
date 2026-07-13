/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include <cstdlib>
#include <string>

namespace zlink::framework::e2e::runtime_monitoring::trigger
{

inline std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

struct trigger_options_t
{
    std::string rid = env_or ("ZLINK_CPP_E2E_RID", "trigger");
    std::string http_endpoint = env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    std::string redis_endpoint = env_or ("ZLINK_CPP_E2E_REDIS_ENDPOINT");
    std::string redis_key_prefix = env_or ("ZLINK_CPP_E2E_REDIS_KEY_PREFIX");
    std::string service_channel_endpoint = env_or ("ZLINK_CPP_E2E_SERVICE_CHANNEL_ENDPOINT");
    std::string service_b_channel_endpoint = env_or ("ZLINK_CPP_E2E_SERVICE_B_CHANNEL_ENDPOINT");
    std::string throw_channel_endpoint = env_or ("ZLINK_CPP_E2E_THROW_CHANNEL_ENDPOINT");
    std::string evidence_file = env_or ("ZLINK_CPP_E2E_EVIDENCE_FILE");
    std::string log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR");
};

inline trigger_options_t read_trigger_options () { return {}; }

} // namespace zlink::framework::e2e::runtime_monitoring::trigger
