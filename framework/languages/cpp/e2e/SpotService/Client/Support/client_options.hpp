/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "client_support.hpp"

#include <string>

namespace zlink::framework::e2e::spot_service::client
{

struct client_options_t
{
    std::string log_dir;
    std::string route_endpoint;
    std::string route_a_endpoint;
    std::string route_b_endpoint;
    std::string multi_route_client_a_endpoint;
    std::string multi_route_client_b_endpoint;
    std::string multi_route_a_endpoint;
    std::string multi_route_b_endpoint;
    std::string spot_router_endpoint;
    std::string pubsub_endpoint;
    std::string publisher_endpoint;
    std::string api_endpoint;
    std::string stream_endpoint;
    std::string alternate_stream_endpoint;
    std::string tls_stream_endpoint;
    std::string scenario_mode;
    std::string play_http_endpoint;
    std::string play_b_http_endpoint;
    std::string multi_a_http_endpoint;
    std::string multi_b_http_endpoint;
    std::string session_http_endpoint;
    std::string gateway_http_endpoint;
    std::string registry_router;
    std::string client_rid;
    std::string crash_ready_file;
    std::string crash_go_file;
    std::string crash_observed_file;

    static client_options_t from_env ()
    {
        return {.log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs"),
                .route_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_ENDPOINT"),
                .route_a_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_A_ENDPOINT"),
                .route_b_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_B_ENDPOINT"),
                .multi_route_client_a_endpoint =
                  env_or ("ZLINK_CPP_E2E_MULTI_ROUTE_CLIENT_A_ENDPOINT"),
                .multi_route_client_b_endpoint =
                  env_or ("ZLINK_CPP_E2E_MULTI_ROUTE_CLIENT_B_ENDPOINT"),
                .multi_route_a_endpoint = env_or ("ZLINK_CPP_E2E_MULTI_ROUTE_A_ENDPOINT"),
                .multi_route_b_endpoint = env_or ("ZLINK_CPP_E2E_MULTI_ROUTE_B_ENDPOINT"),
                .spot_router_endpoint = env_or ("ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT"),
                .pubsub_endpoint = env_or ("ZLINK_CPP_E2E_PUBSUB_ENDPOINT"),
                .publisher_endpoint = env_or ("ZLINK_CPP_E2E_PUBLISHER_ENDPOINT"),
                .api_endpoint = env_or ("ZLINK_CPP_E2E_API_ENDPOINT"),
                .stream_endpoint = env_or ("ZLINK_CPP_E2E_STREAM_ENDPOINT"),
                .alternate_stream_endpoint = env_or ("ZLINK_CPP_E2E_ALT_STREAM_ENDPOINT"),
                .tls_stream_endpoint = env_or ("ZLINK_CPP_E2E_TLS_STREAM_ENDPOINT"),
                .scenario_mode = env_or ("ZLINK_CPP_E2E_SCENARIO_MODE"),
                .play_http_endpoint = env_or ("ZLINK_CPP_E2E_PLAY_HTTP_ENDPOINT"),
                .play_b_http_endpoint = env_or ("ZLINK_CPP_E2E_PLAY_B_HTTP_ENDPOINT"),
                .multi_a_http_endpoint = env_or ("ZLINK_CPP_E2E_MULTI_A_HTTP_ENDPOINT"),
                .multi_b_http_endpoint = env_or ("ZLINK_CPP_E2E_MULTI_B_HTTP_ENDPOINT"),
                .session_http_endpoint = env_or ("ZLINK_CPP_E2E_SESSION_HTTP_ENDPOINT"),
                .gateway_http_endpoint = env_or ("ZLINK_CPP_E2E_GATEWAY_HTTP_ENDPOINT"),
                .registry_router = env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER"),
                .client_rid = env_or ("ZLINK_CPP_E2E_CLIENT_RID", "client-session"),
                .crash_ready_file = env_or ("ZLINK_CPP_E2E_CRASH_READY_FILE"),
                .crash_go_file = env_or ("ZLINK_CPP_E2E_CRASH_GO_FILE"),
                .crash_observed_file = env_or ("ZLINK_CPP_E2E_CRASH_OBSERVED_FILE")};
    }
};

} // namespace zlink::framework::e2e::spot_service::client
