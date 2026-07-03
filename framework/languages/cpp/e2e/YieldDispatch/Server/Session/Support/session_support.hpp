/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../../Shared/env.hpp"

#include <string>

namespace zlink::framework::e2e::yield_dispatch::server::session {

struct session_options_t
{
    std::string log_dir;
    std::string node_rid;
    std::string http_endpoint;
    std::string redis_endpoint;
    std::string redis_key_prefix;
    std::string control_endpoint;
    std::string control_peer_endpoint;
    std::string spot_route_endpoint;
    std::string spot_route_peer_endpoint;
    std::string spot_router_endpoint;
    std::string spot_pub_endpoint;
    std::string stream_endpoint;
};

inline session_options_t read_session_options ()
{
    return session_options_t{
      .log_dir = server::env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs"),
      .node_rid = server::env_or ("ZLINK_CPP_E2E_NODE_RID", "session-a"),
      .http_endpoint = server::env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT"),
      .redis_endpoint = server::env_or ("ZLINK_CPP_E2E_REDIS_ENDPOINT"),
      .redis_key_prefix = server::env_or ("ZLINK_CPP_E2E_REDIS_KEY_PREFIX"),
      .control_endpoint = server::env_or ("ZLINK_CPP_E2E_CONTROL_ENDPOINT"),
      .control_peer_endpoint = server::env_or ("ZLINK_CPP_E2E_CONTROL_PEER_ENDPOINT", ""),
      .spot_route_endpoint = server::env_or ("ZLINK_CPP_E2E_SPOT_ROUTE_ENDPOINT"),
      .spot_route_peer_endpoint = server::env_or ("ZLINK_CPP_E2E_SPOT_ROUTE_PEER_ENDPOINT", ""),
      .spot_router_endpoint = server::env_or ("ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT"),
      .spot_pub_endpoint = server::env_or ("ZLINK_CPP_E2E_SPOT_PUB_ENDPOINT"),
      .stream_endpoint = server::env_or ("ZLINK_CPP_E2E_STREAM_ENDPOINT")};
}

} // namespace zlink::framework::e2e::yield_dispatch::server::session
