/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework.hpp>

#include <string>

namespace zlink::samples::shoppingmall
{

struct sample_names_t
{
    static constexpr const char *order_spot_discovery = "shoppingmall.order.spot";
    static constexpr const char *order_workflow_spot = "shoppingmall.order.workflow.spot";
    static constexpr const char *order_workflow_channel_prefix =
      "shoppingmall.order.workflow.";
    static constexpr const char *order_workflow_spot_route_channel_prefix =
      "shoppingmall.order.workflow.spot.route.";
    static constexpr const char *order_spot_route = "shoppingmall.order.spot.route";
};

inline std::string order_workflow_channel_for (const std::string &instance_id)
{
    return std::string (sample_names_t::order_workflow_channel_prefix) + instance_id;
}

inline std::string order_workflow_spot_route_channel_for (const std::string &instance_id)
{
    return std::string (sample_names_t::order_workflow_spot_route_channel_prefix) + instance_id;
}

struct api_instance_topology_t
{
    std::string instance_id;
    std::string http_url;
    std::string route_endpoint;
    std::string spot_router_endpoint;
    zlink::routing_id_t route_rid;
    zlink::routing_id_t spot_rid;
};

struct workflow_instance_topology_t
{
    std::string instance_id;
    std::string http_url;
    std::string route_endpoint;
    std::string spot_route_endpoint;
    std::string spot_endpoint;
    std::string spot_router_endpoint;
    zlink::routing_id_t route_rid;
    zlink::routing_id_t spot_rid;
    int owner_index{};
};

struct sample_topology_t
{
    std::string redis_endpoint;
    std::string redis_key_prefix;
    std::string api_a_http_url;
    std::string api_b_http_url;
    std::string api_a_route_endpoint;
    std::string api_b_route_endpoint;
    std::string api_a_spot_router_endpoint;
    std::string api_b_spot_router_endpoint;
    std::string workflow_a_http_url;
    std::string workflow_b_http_url;
    std::string workflow_a_route_endpoint;
    std::string workflow_b_route_endpoint;
    std::string workflow_a_spot_route_endpoint;
    std::string workflow_b_spot_route_endpoint;
    std::string workflow_a_spot_endpoint;
    std::string workflow_a_spot_router_endpoint;
    std::string workflow_b_spot_endpoint;
    std::string workflow_b_spot_router_endpoint;

    static sample_topology_t bind (const zlink::framework::configuration_section_t &section)
    {
        sample_topology_t topology;
        topology.redis_endpoint = section.require ("redisEndpoint");
        topology.redis_key_prefix = section.require ("redisKeyPrefix");
        topology.api_a_http_url = section.require ("apiAHttpUrl");
        topology.api_b_http_url = section.require ("apiBHttpUrl");
        topology.api_a_route_endpoint = section.require ("apiARouteEndpoint");
        topology.api_b_route_endpoint = section.require ("apiBRouteEndpoint");
        topology.api_a_spot_router_endpoint = section.require ("apiASpotRouterEndpoint");
        topology.api_b_spot_router_endpoint = section.require ("apiBSpotRouterEndpoint");
        topology.workflow_a_http_url = section.require ("workflowAHttpUrl");
        topology.workflow_b_http_url = section.require ("workflowBHttpUrl");
        topology.workflow_a_route_endpoint = section.require ("workflowARouteEndpoint");
        topology.workflow_b_route_endpoint = section.require ("workflowBRouteEndpoint");
        topology.workflow_a_spot_route_endpoint = section.require ("workflowASpotRouteEndpoint");
        topology.workflow_b_spot_route_endpoint = section.require ("workflowBSpotRouteEndpoint");
        topology.workflow_a_spot_endpoint = section.require ("workflowASpotEndpoint");
        topology.workflow_a_spot_router_endpoint = section.require ("workflowASpotRouterEndpoint");
        topology.workflow_b_spot_endpoint = section.require ("workflowBSpotEndpoint");
        topology.workflow_b_spot_router_endpoint = section.require ("workflowBSpotRouterEndpoint");
        return topology;
    }

    api_instance_topology_t for_api_instance (const std::string &instance_id) const
    {
        if (instance_id == "api-b") {
            return {"api-b",
                    api_b_http_url,
                    api_b_route_endpoint,
                    api_b_spot_router_endpoint,
                    zlink::routing_id_t::from ("6002"),
                    zlink::routing_id_t::from ("6112")};
        }
        return {"api-a",
                api_a_http_url,
                api_a_route_endpoint,
                api_a_spot_router_endpoint,
                zlink::routing_id_t::from ("6001"),
                zlink::routing_id_t::from ("6111")};
    }

    workflow_instance_topology_t for_workflow_instance (const std::string &instance_id) const
    {
        if (instance_id == "workflow-b") {
            return {"workflow-b",
                    workflow_b_http_url,
                    workflow_b_route_endpoint,
                    workflow_b_spot_route_endpoint,
                    workflow_b_spot_endpoint,
                    workflow_b_spot_router_endpoint,
                    zlink::routing_id_t::from ("6202"),
                    zlink::routing_id_t::from ("6102"),
                    1};
        }
        return {"workflow-a",
                workflow_a_http_url,
                workflow_a_route_endpoint,
                workflow_a_spot_route_endpoint,
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
