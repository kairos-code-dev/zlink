/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/server_support.hpp"

namespace zlink::framework::e2e::pubsub::server::subscriber
{

struct subscriber_options_t
{
    std::string log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    std::string subscriber_id = env_or ("ZLINK_CPP_E2E_SUBSCRIBER_ID", "sub-1");
    std::string topics = env_or ("ZLINK_CPP_E2E_TOPICS", "fanout");
    std::string accepted_topics = env_or ("ZLINK_CPP_E2E_ACCEPTED_TOPICS", topics);
    std::string redis_endpoint = env_or ("ZLINK_CPP_E2E_REDIS_ENDPOINT");
    std::string redis_key_prefix = env_or ("ZLINK_CPP_E2E_REDIS_KEY_PREFIX");
    std::string http_endpoint = env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    int handler_delay_ms = std::stoi (env_or ("ZLINK_CPP_E2E_HANDLER_DELAY_MS", "0"));
};

} // namespace zlink::framework::e2e::pubsub::server::subscriber
