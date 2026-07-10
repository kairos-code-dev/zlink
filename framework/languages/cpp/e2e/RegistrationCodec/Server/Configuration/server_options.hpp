/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <cstdlib>
#include <string>

namespace zlink::framework::e2e::registration_codec::server
{

inline std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

struct server_options_t
{
    std::string log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    std::string api_endpoint = env_or ("ZLINK_CPP_E2E_API_ENDPOINT");
    std::string http_endpoint = env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    std::string invalid_mode = env_or ("ZLINK_CPP_E2E_INVALID_MODE");
    std::string server_mode = env_or ("ZLINK_CPP_E2E_SERVER_MODE", "main");
};

} // namespace zlink::framework::e2e::registration_codec::server
