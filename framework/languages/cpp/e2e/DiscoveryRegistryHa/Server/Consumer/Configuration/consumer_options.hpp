/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <cstdlib>
#include <string>

namespace zlink::framework::e2e::store_failure::consumer
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
    std::string redis_endpoint;
    std::string redis_key_prefix;
    std::string log_dir;
};

inline consumer_options_t read_consumer_options ()
{
    return {.rid = env_or ("ZLINK_CPP_DRHA_RID", "consumer-a1"),
            .http_endpoint = env_or ("ZLINK_CPP_DRHA_HTTP_ENDPOINT"),
            .redis_endpoint = env_or ("ZLINK_CPP_E2E_REDIS_ENDPOINT"),
            .redis_key_prefix = env_or ("ZLINK_CPP_E2E_REDIS_KEY_PREFIX"),
            .log_dir = env_or ("ZLINK_CPP_DRHA_LOG_DIR", "logs")};
}

} // namespace zlink::framework::e2e::store_failure::consumer
