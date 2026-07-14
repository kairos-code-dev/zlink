/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include <cstdlib>
#include <string>

namespace zlink::framework::e2e::runtime_monitoring::client
{

inline std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

struct client_options_t
{
    std::string scenario;
    std::string service_url;
    std::string filtered_service_url;
    std::string throw_service_url;
    std::string trigger_url;
    std::string log_dir;
    std::string old_service_channel_endpoint;
    std::string new_service_channel_endpoint;
};

inline client_options_t read_client_options ()
{
    return client_options_t{
      .scenario = env_or ("ZLINK_CPP_E2E_SCENARIO", "common"),
      .service_url = env_or ("ZLINK_CPP_E2E_SERVICE_URL"),
      .filtered_service_url = env_or ("ZLINK_CPP_E2E_FILTERED_SERVICE_URL"),
      .throw_service_url = env_or ("ZLINK_CPP_E2E_THROW_SERVICE_URL"),
      .trigger_url = env_or ("ZLINK_CPP_E2E_TRIGGER_URL"),
      .log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR"),
      .old_service_channel_endpoint = env_or ("ZLINK_CPP_E2E_OLD_SERVICE_CHANNEL_ENDPOINT"),
      .new_service_channel_endpoint = env_or ("ZLINK_CPP_E2E_NEW_SERVICE_CHANNEL_ENDPOINT"),
    };
}

} // namespace zlink::framework::e2e::runtime_monitoring::client
