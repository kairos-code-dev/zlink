/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/server_support.hpp"

namespace zlink::framework::e2e::pubsub::server::registry
{

struct registry_options_t
{
    std::string log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    std::string pub_endpoint = env_or ("ZLINK_CPP_E2E_REGISTRY_PUB");
    std::string router_endpoint = env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER");
    std::string http_endpoint = env_or ("ZLINK_CPP_E2E_REGISTRY_HTTP_ENDPOINT");
};

} // namespace zlink::framework::e2e::pubsub::server::registry
