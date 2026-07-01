/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace zlink::framework::e2e::resilience_lifecycle::provider
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

inline std::optional<int> parse_int_env (const char *name)
{
    const auto value = env_or (name);
    if (value.empty ()) {
        return std::nullopt;
    }
    return std::stoi (value);
}

struct provider_options_t
{
    std::string rid;
    std::string instance_id;
    std::string api_endpoint;
    std::string route_endpoint;
    std::string http_endpoint;
    std::string registry_router;
    std::string embedded_registry_pub;
    std::string embedded_registry_router;
    std::vector<std::string> embedded_registry_peers;
    std::string evidence_file;
    std::string log_dir;
    std::optional<int> server_weight;
    std::optional<int> max_message_size;
};

inline provider_options_t read_provider_options ()
{
    auto rid = env_or ("ZLINK_CPP_E2E_PROVIDER_RID", "api-a");
    return {.rid = rid,
            .instance_id = env_or ("ZLINK_CPP_E2E_PROVIDER_INSTANCE", rid),
            .api_endpoint = env_or ("ZLINK_CPP_E2E_API_ENDPOINT"),
            .route_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_ENDPOINT"),
            .http_endpoint = env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT"),
            .registry_router = env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER"),
            .embedded_registry_pub = env_or ("ZLINK_CPP_E2E_EMBEDDED_REGISTRY_PUB"),
            .embedded_registry_router = env_or ("ZLINK_CPP_E2E_EMBEDDED_REGISTRY_ROUTER"),
            .embedded_registry_peers =
              split_csv (env_or ("ZLINK_CPP_E2E_EMBEDDED_REGISTRY_PEERS")),
            .evidence_file = env_or ("ZLINK_CPP_E2E_EVIDENCE_FILE"),
            .log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs"),
            .server_weight = parse_int_env ("ZLINK_CPP_E2E_SERVER_WEIGHT"),
            .max_message_size = parse_int_env ("ZLINK_CPP_E2E_MAX_MESSAGE_SIZE")};
}

} // namespace zlink::framework::e2e::resilience_lifecycle::provider
