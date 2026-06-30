/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/stream_connector.hpp>

#include <chrono>
#include <cstdlib>
#include <string>

namespace zlink::framework::e2e::yield_dispatch::client
{

struct client_options_t
{
    std::string session_a_stream_endpoint;
};

inline std::string env_or (const char *name)
{
    if (const char *value = std::getenv (name); value != nullptr) {
        return value;
    }
    return {};
}

inline client_options_t parse_client_options ()
{
    return client_options_t{.session_a_stream_endpoint =
                              env_or ("ZLINK_CPP_E2E_STREAM_ENDPOINT")};
}

inline zlink::stream_connector::connector_options_t
make_connector_options (const client_options_t &options)
{
    zlink::stream_connector::connector_options_t connector_options;
    connector_options.endpoint = options.session_a_stream_endpoint;
    connector_options.connect_timeout = std::chrono::milliseconds (5000);
    connector_options.request_timeout = std::chrono::milliseconds (10000);
    connector_options.dispatch_mode = zlink::stream_connector::dispatch_mode_t::immediate;
    return connector_options;
}

} // namespace zlink::framework::e2e::yield_dispatch::client
