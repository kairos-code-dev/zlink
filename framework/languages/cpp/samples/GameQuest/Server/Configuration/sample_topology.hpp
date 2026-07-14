/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "sample_names.hpp"

#include <zlink/framework.hpp>

#include <string>

namespace zlink::samples::gamequest
{

inline int owner_index (const std::string &player_id)
{
    int sum = 0;
    for (const unsigned char value : player_id) {
        sum += static_cast<int> (value);
    }
    return sum % 2;
}

inline zlink::routing_id_t owner_route_rid (const std::string &player_id)
{
    return zlink::routing_id_t::from (
      owner_index (player_id) == 1 ? sample_names_t::mission_b_rid
                                   : sample_names_t::mission_a_rid);
}

inline zlink::framework::spot_rid_t player_spot_rid (const std::string &player_id)
{
    return zlink::framework::spot_rid_t::from_string ("player:" + player_id);
}

inline std::string owner_mission_id (const std::string &player_id)
{
    return owner_index (player_id) == 1 ? "mission-b" : "mission-a";
}

struct sample_topology_t
{
    std::string redis_endpoint;
    std::string redis_key_prefix;
    std::string api_a_stream_endpoint;
    std::string api_b_stream_endpoint;
    std::string api_a_http_url;
    std::string api_b_http_url;
    std::string mission_a_route_endpoint;
    std::string mission_b_route_endpoint;
    std::string mission_a_spot_route_endpoint;
    std::string mission_b_spot_route_endpoint;
    std::string mission_a_spot_router_endpoint;
    std::string mission_b_spot_router_endpoint;
    std::string mission_a_spot_endpoint;
    std::string mission_b_spot_endpoint;
    std::string api_a_spot_router_endpoint;
    std::string api_b_spot_router_endpoint;
    std::string api_name;
    std::string mission_name;
    std::string api_a_spot_route_endpoint;
    std::string api_b_spot_route_endpoint;

    static sample_topology_t bind (const zlink::framework::configuration_section_t &section)
    {
        sample_topology_t topology;
        topology.redis_endpoint = section.require ("redisEndpoint");
        topology.redis_key_prefix = section.require ("redisKeyPrefix");
        topology.api_a_stream_endpoint = section.require ("apiAStreamEndpoint");
        topology.api_b_stream_endpoint = section.require ("apiBStreamEndpoint");
        topology.api_a_http_url = section.require ("apiAHttpUrl");
        topology.api_b_http_url = section.require ("apiBHttpUrl");
        topology.mission_a_route_endpoint = section.require ("missionARouteEndpoint");
        topology.mission_b_route_endpoint = section.require ("missionBRouteEndpoint");
        topology.mission_a_spot_route_endpoint = section.require ("missionASpotRouteEndpoint");
        topology.mission_b_spot_route_endpoint = section.require ("missionBSpotRouteEndpoint");
        topology.mission_a_spot_router_endpoint = section.require ("missionASpotRouterEndpoint");
        topology.mission_b_spot_router_endpoint = section.require ("missionBSpotRouterEndpoint");
        topology.mission_a_spot_endpoint = section.require ("missionASpotEndpoint");
        topology.mission_b_spot_endpoint = section.require ("missionBSpotEndpoint");
        topology.api_a_spot_router_endpoint = section.require ("apiASpotRouterEndpoint");
        topology.api_b_spot_router_endpoint = section.require ("apiBSpotRouterEndpoint");
        topology.api_name = section.require ("apiName");
        topology.mission_name = section.require ("missionName");
        topology.api_a_spot_route_endpoint = section.require ("apiASpotRouteEndpoint");
        topology.api_b_spot_route_endpoint = section.require ("apiBSpotRouteEndpoint");
        return topology;
    }

    std::string selected_api_stream_endpoint () const
    {
        return api_name == "api-b" ? api_b_stream_endpoint : api_a_stream_endpoint;
    }

    std::string selected_api_http_url () const
    {
        return api_name == "api-b" ? api_b_http_url : api_a_http_url;
    }


    std::string selected_mission_route_endpoint () const
    {
        return mission_name == "mission-b" ? mission_b_route_endpoint : mission_a_route_endpoint;
    }

    std::string selected_mission_spot_route_endpoint () const
    {
        return mission_name == "mission-b" ? mission_b_spot_route_endpoint
                                           : mission_a_spot_route_endpoint;
    }

    std::string selected_mission_spot_router_endpoint () const
    {
        return mission_name == "mission-b" ? mission_b_spot_router_endpoint
                                           : mission_a_spot_router_endpoint;
    }

    std::string selected_mission_spot_endpoint () const
    {
        return mission_name == "mission-b" ? mission_b_spot_endpoint : mission_a_spot_endpoint;
    }



    zlink::routing_id_t selected_mission_rid () const
    {
        return zlink::routing_id_t::from (mission_name == "mission-b"
                                            ? sample_names_t::mission_b_rid
                                            : sample_names_t::mission_a_rid);
    }


    std::string selected_api_spot_router_endpoint () const
    {
        return api_name == "api-b" ? api_b_spot_router_endpoint : api_a_spot_router_endpoint;
    }

    /* spot route mesh는 양방향이다: API는 owner spot으로 gameplay를 보내고, owner spot은 같은
     * mesh로 player의 현재 노드에 notify를 보낸다. spot 노드는 route 채널 하나에만 bridge를
     * 붙이므로, 별도 mesh를 더 두면 그쪽으로 온 프레임은 spot에 닿지 못한다. */
    std::string selected_api_spot_route_endpoint () const
    {
        return api_name == "api-b" ? api_b_spot_route_endpoint : api_a_spot_route_endpoint;
    }

    std::string selected_api_node_rid () const
    {
        return api_name == "api-b" ? sample_names_t::api_b_rid : sample_names_t::api_a_rid;
    }

    zlink::routing_id_t selected_api_rid () const
    {
        return zlink::routing_id_t::from (selected_api_node_rid ());
    }
};

} // namespace zlink::samples::gamequest
