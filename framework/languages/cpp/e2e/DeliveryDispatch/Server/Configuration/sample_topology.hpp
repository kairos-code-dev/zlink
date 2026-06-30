/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework.hpp>

#include <cstdlib>
#include <string>

namespace zlink::samples::deliverydispatch
{

inline std::string env_or (const char *name, std::string fallback)
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

inline std::string deliverydispatch_log_dir ()
{
    return env_or ("DELIVERYDISPATCH_LOG_DIR", "logs");
}

struct sample_topology_t
{
    std::string registry_router_endpoint = env_or ("DELIVERYDISPATCH_REGISTRY", "tcp://127.0.0.1:7391");
    std::string registry_pub_endpoint = env_or ("DELIVERYDISPATCH_REGISTRY_PUB", "tcp://127.0.0.1:7390");
    std::string dispatch_api_http_url = env_or ("DELIVERYDISPATCH_API_HTTP", "http://127.0.0.1:7392");
    std::string dispatch_center_route_endpoint =
      env_or ("DELIVERYDISPATCH_CENTER_ROUTE", "tcp://127.0.0.1:7394");
    std::string courier_a_endpoint =
      env_or ("DELIVERYDISPATCH_COURIER_A_ROUTE", "tcp://127.0.0.1:7395");
    std::string courier_b_endpoint =
      env_or ("DELIVERYDISPATCH_COURIER_B_ROUTE", "tcp://127.0.0.1:7396");
    std::string tracking_route_endpoint =
      env_or ("DELIVERYDISPATCH_TRACKING_ROUTE", "tcp://127.0.0.1:7397");
    std::string status_fanout_endpoint =
      env_or ("DELIVERYDISPATCH_STATUS_FANOUT", "tcp://127.0.0.1:7411");
    std::string session_stream_endpoint =
      env_or ("DELIVERYDISPATCH_SESSION_STREAM", "tcp://127.0.0.1:7400");
};

inline int port_from_http_url (const std::string &url)
{
    const auto colon = url.rfind (':');
    if (colon == std::string::npos) {
        return 80;
    }
    return std::stoi (url.substr (colon + 1));
}

} // namespace zlink::samples::deliverydispatch
