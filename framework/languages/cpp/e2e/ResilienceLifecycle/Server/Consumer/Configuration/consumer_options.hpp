/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace zlink::framework::e2e::resilience_lifecycle::consumer
{

inline std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

inline std::vector<std::string> split_csv (const std::string &text)
{
    std::vector<std::string> result;
    std::stringstream input (text);
    std::string item;
    while (std::getline (input, item, ',')) {
        if (!item.empty ()) {
            result.push_back (item);
        }
    }
    return result;
}

struct consumer_options_t
{
    std::string http_endpoint;
    std::string redis_endpoint;
    std::string redis_key_prefix;
    std::vector<std::string> provider_endpoints;
    std::string log_dir;
    std::string trace_label;
};

inline consumer_options_t read_consumer_options ()
{
    return {.http_endpoint = env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT"),
            .redis_endpoint = env_or ("ZLINK_CPP_E2E_REDIS_ENDPOINT"),
            .redis_key_prefix = env_or ("ZLINK_CPP_E2E_REDIS_KEY_PREFIX"),
            .provider_endpoints = split_csv (env_or ("ZLINK_CPP_E2E_PROVIDER_ENDPOINTS")),
            .log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs"),
            .trace_label = env_or ("ZLINK_CPP_E2E_TRACE_LABEL", "consumer")};
}

} // namespace zlink::framework::e2e::resilience_lifecycle::consumer
