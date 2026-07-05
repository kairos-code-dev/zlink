/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <cstdlib>
#include <string>

namespace zlink::samples::supportchat
{

inline std::string env_or (const char *name, std::string fallback)
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

inline std::string supportchat_log_dir ()
{
    return env_or ("SUPPORTCHAT_LOG_DIR", "logs");
}

struct sample_topology_t
{
    std::string redis_endpoint = env_or ("SUPPORTCHAT_REDIS_ENDPOINT", "");
    std::string redis_key_prefix = env_or ("SUPPORTCHAT_REDIS_KEY_PREFIX", "");
    std::string api_route_endpoint =
      env_or ("SUPPORTCHAT_API_ROUTE", "tcp://127.0.0.1:7501");
    std::string support_route_endpoint =
      env_or ("SUPPORTCHAT_SUPPORT_ROUTE", "tcp://127.0.0.1:7502");
    std::string support_spot_router_endpoint =
      env_or ("SUPPORTCHAT_SUPPORT_SPOT_ROUTER", "tcp://127.0.0.1:7503");
    std::string support_spot_endpoint =
      env_or ("SUPPORTCHAT_SUPPORT_SPOT", "tcp://127.0.0.1:7504");
    std::string session_stream_endpoint =
      env_or ("SUPPORTCHAT_SESSION_STREAM", "tcp://127.0.0.1:7505");
    std::string session_spot_router_endpoint =
      env_or ("SUPPORTCHAT_SESSION_SPOT_ROUTER", "tcp://127.0.0.1:7506");
    std::string session_spot_endpoint =
      env_or ("SUPPORTCHAT_SESSION_SPOT", "tcp://127.0.0.1:7507");
};

} // namespace zlink::samples::supportchat
