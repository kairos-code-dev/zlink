/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../../Shared/env.hpp"

#include <string>

namespace zlink::framework::e2e::yield_dispatch::server::registry {

struct registry_options_t
{
    std::string log_dir;
    std::string http_endpoint;
    std::string pub_endpoint;
    std::string router_endpoint;
};

inline registry_options_t read_registry_options ()
{
    return registry_options_t{
      .log_dir = server::env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs"),
      .http_endpoint = server::env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT"),
      .pub_endpoint = server::env_or ("ZLINK_CPP_E2E_REGISTRY_PUB"),
      .router_endpoint = server::env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER")};
}

} // namespace zlink::framework::e2e::yield_dispatch::server::registry
