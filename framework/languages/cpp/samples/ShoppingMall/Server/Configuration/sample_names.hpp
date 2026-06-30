/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <cstdlib>
#include <string>

namespace zlink::samples::shoppingmall
{

struct sample_names_t
{
    static constexpr const char *workflow_route_channel = "shoppingmall.order.workflow.route";
};

inline std::string shoppingmall_env_or (const char *name, std::string fallback)
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

inline std::string shoppingmall_log_dir ()
{
    return shoppingmall_env_or ("SHOPPINGMALL_LOG_DIR", "logs");
}

struct sample_topology_t
{
    std::string registry_pub =
      shoppingmall_env_or ("SHOPPINGMALL_REGISTRY_PUB_ENDPOINT", "tcp://127.0.0.1:48201");
    std::string registry_router =
      shoppingmall_env_or ("SHOPPINGMALL_REGISTRY_ROUTER_ENDPOINT", "tcp://127.0.0.1:48202");
    std::string api_a_http =
      shoppingmall_env_or ("SHOPPINGMALL_API_A_HTTP_URL", "http://127.0.0.1:48203");
    std::string api_b_http =
      shoppingmall_env_or ("SHOPPINGMALL_API_B_HTTP_URL", "http://127.0.0.1:48204");
    std::string api_a_route =
      shoppingmall_env_or ("SHOPPINGMALL_API_A_ROUTE_ENDPOINT", "tcp://127.0.0.1:48205");
    std::string api_b_route =
      shoppingmall_env_or ("SHOPPINGMALL_API_B_ROUTE_ENDPOINT", "tcp://127.0.0.1:48206");
    std::string workflow_a_http =
      shoppingmall_env_or ("SHOPPINGMALL_WORKFLOW_A_HTTP_URL", "http://127.0.0.1:48207");
    std::string workflow_b_http =
      shoppingmall_env_or ("SHOPPINGMALL_WORKFLOW_B_HTTP_URL", "http://127.0.0.1:48208");
    std::string workflow_a_route =
      shoppingmall_env_or ("SHOPPINGMALL_WORKFLOW_A_ROUTE_ENDPOINT", "tcp://127.0.0.1:48209");
    std::string workflow_b_route =
      shoppingmall_env_or ("SHOPPINGMALL_WORKFLOW_B_ROUTE_ENDPOINT", "tcp://127.0.0.1:48210");

    std::string route_rid_for_api (const std::string &instance) const
    {
        return instance == "api-b" ? "6002" : "6001";
    }

    std::string route_rid_for_workflow (const std::string &instance) const
    {
        return instance == "workflow-b" ? "6202" : "6201";
    }

    std::string route_endpoint_for_api (const std::string &instance) const
    {
        return instance == "api-b" ? api_b_route : api_a_route;
    }

    std::string route_endpoint_for_workflow (const std::string &instance) const
    {
        return instance == "workflow-b" ? workflow_b_route : workflow_a_route;
    }

    std::string workflow_rid_for_order (const std::string &order_id) const
    {
        int sum = 0;
        for (const auto ch : order_id) {
            sum = (sum * 31 + static_cast<unsigned char> (ch)) & 0x7fffffff;
        }
        return (sum % 2) == 1 ? "6202" : "6201";
    }
};

inline int port_from_http_url (const std::string &url)
{
    const auto colon = url.rfind (':');
    return colon == std::string::npos ? 80 : std::stoi (url.substr (colon + 1));
}

} // namespace zlink::samples::shoppingmall
