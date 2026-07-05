/* SPDX-License-Identifier: MPL-2.0 */
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

    std::string selected_mission_route_endpoint () const
    {
        return mission_name == "mission-b" ? mission_b_route_endpoint
                                           : mission_a_route_endpoint;
    }

    zlink::routing_id_t selected_mission_rid () const
    {
        return zlink::routing_id_t::from (mission_name == "mission-b"
                                            ? sample_names_t::mission_b_rid
                                            : sample_names_t::mission_a_rid);
    }
};

} // namespace zlink::samples::gamequest
