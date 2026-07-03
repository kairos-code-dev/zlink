/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/server_support.hpp"

namespace zlink::framework::e2e::pubsub::server::publisher
{

struct publisher_options_t
{
    std::string log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    std::string redis_endpoint = env_or ("ZLINK_CPP_E2E_REDIS_ENDPOINT");
    std::string redis_key_prefix = env_or ("ZLINK_CPP_E2E_REDIS_KEY_PREFIX");
    std::string publisher_endpoint = env_or ("ZLINK_CPP_E2E_PUBLISHER_ENDPOINT");
    std::string http_endpoint = env_or ("ZLINK_CPP_E2E_PUBLISHER_HTTP_ENDPOINT");
};

} // namespace zlink::framework::e2e::pubsub::server::publisher
