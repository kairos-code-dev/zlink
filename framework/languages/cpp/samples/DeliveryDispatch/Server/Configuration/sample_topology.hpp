/* SPDX-License-Identifier: FSL-1.1-ALv2 */
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
    std::string redis_endpoint = env_or ("DELIVERYDISPATCH_REDIS_ENDPOINT", "");
    std::string redis_key_prefix = env_or ("DELIVERYDISPATCH_REDIS_KEY_PREFIX", "");
    std::string dispatch_api_http_url = env_or ("DELIVERYDISPATCH_API_HTTP", "http://127.0.0.1:7392");
    std::string dispatch_route_endpoint =
      env_or ("DELIVERYDISPATCH_DISPATCH_ROUTE",
              env_or ("DELIVERYDISPATCH_CENTER_ROUTE", "tcp://127.0.0.1:7394"));
    std::string dispatch_spot_router_endpoint =
      env_or ("DELIVERYDISPATCH_DISPATCH_SPOT_ROUTER", "tcp://127.0.0.1:7395");
    std::string dispatch_spot_endpoint =
      env_or ("DELIVERYDISPATCH_DISPATCH_SPOT", "tcp://127.0.0.1:7396");
    std::string tracking_route_endpoint =
      env_or ("DELIVERYDISPATCH_TRACKING_ROUTE", "tcp://127.0.0.1:7397");
    std::string tracking_spot_router_endpoint =
      env_or ("DELIVERYDISPATCH_TRACKING_SPOT_ROUTER", "tcp://127.0.0.1:7413");
    std::string tracking_spot_endpoint =
      env_or ("DELIVERYDISPATCH_TRACKING_SPOT", "tcp://127.0.0.1:7414");
    std::string customer_stream_endpoint =
      env_or ("DELIVERYDISPATCH_CUSTOMER_STREAM",
              env_or ("DELIVERYDISPATCH_SESSION_STREAM", "tcp://127.0.0.1:7400"));
    std::string customer_spot_router_endpoint =
      env_or ("DELIVERYDISPATCH_CUSTOMER_SPOT_ROUTER",
              env_or ("DELIVERYDISPATCH_SESSION_SPOT_ROUTER", "tcp://127.0.0.1:7399"));
    std::string customer_spot_endpoint =
      env_or ("DELIVERYDISPATCH_CUSTOMER_SPOT",
              env_or ("DELIVERYDISPATCH_SESSION_SPOT", "tcp://127.0.0.1:7401"));
    std::string courier_stream_endpoint =
      env_or ("DELIVERYDISPATCH_COURIER_STREAM", "tcp://127.0.0.1:7402");
    std::string session_stream_endpoint =
      env_or ("DELIVERYDISPATCH_SESSION_STREAM", "tcp://127.0.0.1:7400");
    std::string courier_session_route_endpoint =
      env_or ("DELIVERYDISPATCH_COURIER_SESSION_ROUTE", "tcp://127.0.0.1:7403");
    std::string courier_session_spot_router_endpoint =
      env_or ("DELIVERYDISPATCH_COURIER_SESSION_SPOT_ROUTER", "tcp://127.0.0.1:7404");
    std::string courier_session_spot_endpoint =
      env_or ("DELIVERYDISPATCH_COURIER_SESSION_SPOT", "tcp://127.0.0.1:7412");
    std::string courier_actor_node_1_route_endpoint =
      env_or ("DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTE", "tcp://127.0.0.1:7405");
    std::string courier_actor_node_1_router_endpoint =
      env_or ("DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTER", "tcp://127.0.0.1:7406");
    std::string courier_actor_node_1_endpoint =
      env_or ("DELIVERYDISPATCH_COURIER_ACTOR_NODE1", "tcp://127.0.0.1:7407");
    std::string courier_actor_node_2_route_endpoint =
      env_or ("DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTE", "tcp://127.0.0.1:7408");
    std::string courier_actor_node_2_router_endpoint =
      env_or ("DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTER", "tcp://127.0.0.1:7409");
    std::string courier_actor_node_2_endpoint =
      env_or ("DELIVERYDISPATCH_COURIER_ACTOR_NODE2", "tcp://127.0.0.1:7410");
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
