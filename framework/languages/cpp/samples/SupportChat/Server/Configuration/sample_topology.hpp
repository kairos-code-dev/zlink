/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework.hpp>

#include <string>

namespace zlink::samples::supportchat
{

struct sample_topology_t
{
    std::string redis_endpoint;
    std::string redis_key_prefix;
    std::string api_route_endpoint;
    std::string support_route_endpoint;
    std::string support_spot_router_endpoint;
    std::string support_spot_endpoint;
    std::string support_http_url;
    std::string support_actor_route_endpoint;
    std::string session_stream_endpoint;
    std::string session_spot_router_endpoint;
    std::string session_spot_endpoint;
    std::string session_actor_route_endpoint;

    static sample_topology_t bind (const zlink::framework::configuration_section_t &section)
    {
        sample_topology_t topology;
        topology.redis_endpoint = section.require ("redisEndpoint");
        topology.redis_key_prefix = section.require ("redisKeyPrefix");
        topology.api_route_endpoint = section.require ("apiRouteEndpoint");
        topology.support_route_endpoint = section.require ("supportRouteEndpoint");
        topology.support_spot_router_endpoint = section.require ("supportSpotRouterEndpoint");
        topology.support_spot_endpoint = section.require ("supportSpotEndpoint");
        topology.support_http_url = section.require ("supportHttpUrl");
        topology.support_actor_route_endpoint = section.require ("supportActorRouteEndpoint");
        topology.session_stream_endpoint = section.require ("sessionStreamEndpoint");
        topology.session_spot_router_endpoint = section.require ("sessionSpotRouterEndpoint");
        topology.session_spot_endpoint = section.require ("sessionSpotEndpoint");
        topology.session_actor_route_endpoint = section.require ("sessionActorRouteEndpoint");
        return topology;
    }
};

} // namespace zlink::samples::supportchat
