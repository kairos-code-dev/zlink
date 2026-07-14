/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "sample_names.hpp"

#include <zlink/framework.hpp>

#include <cstdlib>
#include <string>

namespace zlink::samples::gamequest
{

inline std::string env_or (const char *name, std::string fallback)
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

inline std::string gamequest_log_dir ()
{
    return env_or ("GAMEQUEST_LOG_DIR", "logs");
}

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
    std::string redis_endpoint = env_or ("GAMEQUEST_REDIS_ENDPOINT", "");
    std::string redis_key_prefix = env_or ("GAMEQUEST_REDIS_KEY_PREFIX", "");
    std::string api_a_stream_endpoint =
      env_or ("GAMEQUEST_API_A_STREAM_ENDPOINT", "tcp://127.0.0.1:7421");
    std::string api_b_stream_endpoint =
      env_or ("GAMEQUEST_API_B_STREAM_ENDPOINT", "tcp://127.0.0.1:7422");
    std::string api_a_http_url =
      env_or ("GAMEQUEST_API_A_HTTP_URL", "http://127.0.0.1:7423");
    std::string api_b_http_url =
      env_or ("GAMEQUEST_API_B_HTTP_URL", "http://127.0.0.1:7424");
    std::string mission_a_route_endpoint =
      env_or ("GAMEQUEST_MISSION_A_ROUTE_ENDPOINT", "tcp://127.0.0.1:7425");
    std::string mission_b_route_endpoint =
      env_or ("GAMEQUEST_MISSION_B_ROUTE_ENDPOINT", "tcp://127.0.0.1:7426");
    std::string mission_a_spot_route_endpoint =
      env_or ("GAMEQUEST_MISSION_A_SPOT_ROUTE_ENDPOINT", "tcp://127.0.0.1:7429");
    std::string mission_b_spot_route_endpoint =
      env_or ("GAMEQUEST_MISSION_B_SPOT_ROUTE_ENDPOINT", "tcp://127.0.0.1:7430");
    std::string mission_a_spot_router_endpoint =
      env_or ("GAMEQUEST_MISSION_A_SPOT_ROUTER_ENDPOINT", "tcp://127.0.0.1:7431");
    std::string mission_b_spot_router_endpoint =
      env_or ("GAMEQUEST_MISSION_B_SPOT_ROUTER_ENDPOINT", "tcp://127.0.0.1:7432");
    std::string mission_a_spot_endpoint =
      env_or ("GAMEQUEST_MISSION_A_SPOT_ENDPOINT", "tcp://127.0.0.1:7433");
    std::string mission_b_spot_endpoint =
      env_or ("GAMEQUEST_MISSION_B_SPOT_ENDPOINT", "tcp://127.0.0.1:7434");
    std::string api_a_spot_router_endpoint =
      env_or ("GAMEQUEST_API_A_SPOT_ROUTER_ENDPOINT", "tcp://127.0.0.1:7435");
    std::string api_b_spot_router_endpoint =
      env_or ("GAMEQUEST_API_B_SPOT_ROUTER_ENDPOINT", "tcp://127.0.0.1:7436");
    std::string api_a_route_endpoint =
      env_or ("GAMEQUEST_API_A_ROUTE_ENDPOINT", "tcp://127.0.0.1:7427");
    std::string api_b_route_endpoint =
      env_or ("GAMEQUEST_API_B_ROUTE_ENDPOINT", "tcp://127.0.0.1:7428");

    std::string api_name = env_or ("GAMEQUEST_API_NAME", "api-a");
    std::string mission_name = env_or ("GAMEQUEST_MISSION_NAME", "mission-a");

    std::string selected_api_stream_endpoint () const
    {
        return api_name == "api-b" ? api_b_stream_endpoint : api_a_stream_endpoint;
    }

    std::string selected_api_http_url () const
    {
        return api_name == "api-b" ? api_b_http_url : api_a_http_url;
    }

    std::string api_http_url_for (const std::string &source_api) const
    {
        return source_api == "api-b" ? api_b_http_url : api_a_http_url;
    }

    std::string selected_mission_route_endpoint () const
    {
        return mission_name == "mission-b" ? mission_b_route_endpoint
                                           : mission_a_route_endpoint;
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

    std::string mission_spot_route_endpoint_for (const std::string &mission_id) const
    {
        return mission_id == "mission-b" ? mission_b_spot_route_endpoint
                                         : mission_a_spot_route_endpoint;
    }

    std::string mission_spot_router_endpoint_for (const std::string &mission_id) const
    {
        return mission_id == "mission-b" ? mission_b_spot_router_endpoint
                                         : mission_a_spot_router_endpoint;
    }

    zlink::routing_id_t selected_mission_rid () const
    {
        return zlink::routing_id_t::from (mission_name == "mission-b"
                                            ? sample_names_t::mission_b_rid
                                            : sample_names_t::mission_a_rid);
    }

    std::string selected_api_route_endpoint () const
    {
        return api_name == "api-b" ? api_b_route_endpoint : api_a_route_endpoint;
    }

    std::string selected_api_spot_router_endpoint () const
    {
        return api_name == "api-b" ? api_b_spot_router_endpoint
                                   : api_a_spot_router_endpoint;
    }

    std::string api_a_spot_route_endpoint =
      env_or ("GAMEQUEST_API_A_SPOT_ROUTE", "tcp://127.0.0.1:7621");
    std::string api_b_spot_route_endpoint =
      env_or ("GAMEQUEST_API_B_SPOT_ROUTE", "tcp://127.0.0.1:7622");

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
