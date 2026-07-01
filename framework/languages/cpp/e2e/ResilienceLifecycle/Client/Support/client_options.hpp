/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <cstdlib>
#include <string>

namespace zlink::framework::e2e::resilience_lifecycle::client
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
    std::string log_dir;
    std::string scenario;
    std::string registry_router;
    std::string api_a_endpoint;
    std::string api_b_endpoint;
    std::string route_a_endpoint;
    std::string route_b_endpoint;
    std::string client_route_endpoint;
    std::string http_consumer_endpoint;
    std::string http_b_green_endpoint;
};

inline client_options_t read_client_options ()
{
    return {.log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs"),
            .scenario = env_or ("ZLINK_CPP_E2E_SCENARIO", "common"),
            .registry_router = env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER"),
            .api_a_endpoint = env_or ("ZLINK_CPP_E2E_API_A_ENDPOINT"),
            .api_b_endpoint = env_or ("ZLINK_CPP_E2E_API_B_ENDPOINT"),
            .route_a_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_A_ENDPOINT"),
            .route_b_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_B_ENDPOINT"),
            .client_route_endpoint = env_or ("ZLINK_CPP_E2E_CLIENT_ROUTE_ENDPOINT"),
            .http_consumer_endpoint = env_or ("ZLINK_CPP_E2E_HTTP_CONSUMER_ENDPOINT"),
            .http_b_green_endpoint = env_or ("ZLINK_CPP_E2E_HTTP_B_GREEN_ENDPOINT")};
}

} // namespace zlink::framework::e2e::resilience_lifecycle::client
