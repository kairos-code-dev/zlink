/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework.hpp>

#include <cstdlib>
#include <string>

namespace zlink::samples::shoppingmall
{

inline std::string env_or (const char *name, std::string fallback)
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

inline std::string shoppingmall_log_dir ()
{
    return env_or ("SHOPPINGMALL_LOG_DIR", "logs");
}

inline std::string shoppingmall_state_file ()
{
    return env_or ("SHOPPINGMALL_STATE_FILE", "/tmp/zlink-shoppingmall-cpp-state.json");
}

struct sample_names_t
{
    static constexpr const char *order_spot_discovery = "shoppingmall.order.spot";
    static constexpr const char *order_workflow_route_channel =
      "shoppingmall.order.workflow.route";
};

struct api_instance_topology_t
{
    std::string instance_id;
    std::string http_url;
    std::string route_endpoint;
    zlink::routing_id_t route_rid;
};

struct workflow_instance_topology_t
{
    std::string instance_id;
    std::string http_url;
    std::string route_endpoint;
    std::string spot_endpoint;
    std::string spot_router_endpoint;
    zlink::routing_id_t route_rid;
    zlink::routing_id_t spot_rid;
    int owner_index{};
};

struct sample_topology_t
{
    std::string redis_endpoint = env_or ("SHOPPINGMALL_REDIS_ENDPOINT", "");
    std::string redis_key_prefix = env_or ("SHOPPINGMALL_REDIS_KEY_PREFIX", "shoppingmall:");
    std::string api_a_http_url =
      env_or ("SHOPPINGMALL_API_A_HTTP_URL", "http://127.0.0.1:48203");
    std::string api_b_http_url =
      env_or ("SHOPPINGMALL_API_B_HTTP_URL", "http://127.0.0.1:48204");
    std::string api_a_route_endpoint =
      env_or ("SHOPPINGMALL_API_A_ROUTE_ENDPOINT", "tcp://127.0.0.1:48205");
    std::string api_b_route_endpoint =
      env_or ("SHOPPINGMALL_API_B_ROUTE_ENDPOINT", "tcp://127.0.0.1:48206");
    std::string workflow_a_http_url =
      env_or ("SHOPPINGMALL_WORKFLOW_A_HTTP_URL", "http://127.0.0.1:48207");
    std::string workflow_b_http_url =
      env_or ("SHOPPINGMALL_WORKFLOW_B_HTTP_URL", "http://127.0.0.1:48208");
    std::string workflow_a_route_endpoint =
      env_or ("SHOPPINGMALL_WORKFLOW_A_ROUTE_ENDPOINT", "tcp://127.0.0.1:48209");
    std::string workflow_b_route_endpoint =
      env_or ("SHOPPINGMALL_WORKFLOW_B_ROUTE_ENDPOINT", "tcp://127.0.0.1:48210");
    std::string workflow_a_spot_endpoint =
      env_or ("SHOPPINGMALL_WORKFLOW_A_SPOT_ENDPOINT", "tcp://127.0.0.1:48211");
    std::string workflow_a_spot_router_endpoint =
      env_or ("SHOPPINGMALL_WORKFLOW_A_SPOT_ROUTER_ENDPOINT", "tcp://127.0.0.1:48212");
    std::string workflow_b_spot_endpoint =
      env_or ("SHOPPINGMALL_WORKFLOW_B_SPOT_ENDPOINT", "tcp://127.0.0.1:48213");
    std::string workflow_b_spot_router_endpoint =
      env_or ("SHOPPINGMALL_WORKFLOW_B_SPOT_ROUTER_ENDPOINT", "tcp://127.0.0.1:48214");

    api_instance_topology_t for_api_instance (const std::string &instance_id) const
    {
        if (instance_id == "api-b") {
            return {"api-b", api_b_http_url, api_b_route_endpoint,
                    zlink::routing_id_t::from ("6002")};
        }
        return {"api-a", api_a_http_url, api_a_route_endpoint,
                zlink::routing_id_t::from ("6001")};
    }

    workflow_instance_topology_t for_workflow_instance (const std::string &instance_id) const
    {
        if (instance_id == "workflow-b") {
            return {"workflow-b",
                    workflow_b_http_url,
                    workflow_b_route_endpoint,
                    workflow_b_spot_endpoint,
                    workflow_b_spot_router_endpoint,
                    zlink::routing_id_t::from ("6202"),
                    zlink::routing_id_t::from ("6102"),
                    1};
        }
        return {"workflow-a",
                workflow_a_http_url,
                workflow_a_route_endpoint,
                workflow_a_spot_endpoint,
                workflow_a_spot_router_endpoint,
                zlink::routing_id_t::from ("6201"),
                zlink::routing_id_t::from ("6101"),
                0};
    }

    workflow_instance_topology_t for_order_id (const std::string &order_id) const
    {
        return stable_owner_index (order_id) == 1 ? for_workflow_instance ("workflow-b")
                                                  : for_workflow_instance ("workflow-a");
    }

    static int stable_owner_index (const std::string &order_id)
    {
        int sum = 0;
        for (const auto c : order_id) {
            sum = (sum * 31 + static_cast<unsigned char> (c)) & 0x7fffffff;
        }
        return sum % 2;
    }
};

} // namespace zlink::samples::shoppingmall
