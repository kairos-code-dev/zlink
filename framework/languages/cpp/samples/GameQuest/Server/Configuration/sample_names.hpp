/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <cstdlib>
#include <string>

namespace zlink::samples::gamequest
{

struct sample_names_t
{
    static constexpr const char *fanout_channel = "gamequest.events";
    static constexpr const char *gameplay_topic = "gameplay.event";
    static constexpr const char *stream_node = "gamequest.stream";
    static constexpr const char *first_hunt = "first-hunt";
    static constexpr const char *open_auction = "open-auction";
    static constexpr const char *herb_gathering = "herb-gathering";
    static constexpr const char *reward_granted = "RewardGranted";
    static constexpr const char *active = "Active";
};

inline std::string gamequest_env_or (const char *name, std::string fallback)
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

inline std::string gamequest_log_dir ()
{
    return gamequest_env_or ("GAMEQUEST_LOG_DIR", "logs");
}

struct sample_topology_t
{
    std::string registry_pub_endpoint =
      gamequest_env_or ("GAMEQUEST_REGISTRY_PUB_ENDPOINT", "tcp://127.0.0.1:7410");
    std::string registry_router_endpoint =
      gamequest_env_or ("GAMEQUEST_REGISTRY_ROUTER_ENDPOINT", "tcp://127.0.0.1:7411");
    std::string fanout_publisher_a_endpoint =
      gamequest_env_or ("GAMEQUEST_FANOUT_PUBLISHER_A_ENDPOINT", "tcp://127.0.0.1:7412");
    std::string fanout_publisher_b_endpoint =
      gamequest_env_or ("GAMEQUEST_FANOUT_PUBLISHER_B_ENDPOINT", "tcp://127.0.0.1:7413");
    std::string game_api_a_http =
      gamequest_env_or ("GAMEQUEST_GAMEAPI_A_HTTP_BASE_URL", "http://127.0.0.1:7414");
    std::string game_api_b_http =
      gamequest_env_or ("GAMEQUEST_GAMEAPI_B_HTTP_BASE_URL", "http://127.0.0.1:7415");
    std::string mission_a_http =
      gamequest_env_or ("GAMEQUEST_MISSION_A_HTTP_URL", "http://127.0.0.1:7416");
    std::string mission_b_http =
      gamequest_env_or ("GAMEQUEST_MISSION_B_HTTP_URL", "http://127.0.0.1:7417");
    std::string game_api_a_stream =
      gamequest_env_or ("GAMEQUEST_GAMEAPI_A_STREAM_ENDPOINT", "tcp://127.0.0.1:7418");
    std::string game_api_b_stream =
      gamequest_env_or ("GAMEQUEST_GAMEAPI_B_STREAM_ENDPOINT", "tcp://127.0.0.1:7419");
    std::string store_dir = gamequest_env_or ("GAMEQUEST_STORE_DIR", "logs");
};

inline int port_from_http_url (const std::string &url)
{
    const auto colon = url.rfind (':');
    return colon == std::string::npos ? 80 : std::stoi (url.substr (colon + 1));
}

} // namespace zlink::samples::gamequest
