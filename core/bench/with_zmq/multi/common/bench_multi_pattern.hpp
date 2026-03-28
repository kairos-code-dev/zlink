#ifndef BENCH_MULTI_PATTERN_HPP
#define BENCH_MULTI_PATTERN_HPP

#include <cstring>
#include <zlink.h>
#include "../../common/zlink_api_compat.hpp"

struct multi_pattern_config_t
{
    const char *pattern;
    const char *token;
    int server_socket_type;
    int client_socket_type;
    bool server_router_echo;
    bool client_router_send;
    bool pubsub_mode;
    bool one_way_latency;
    bool server_has_routing_id;
    const char *server_routing_id;
};

inline multi_pattern_config_t multi_pattern_config_for_name (const char *pattern)
{
    multi_pattern_config_t cfg;
    cfg.pattern = pattern;
    cfg.token = "multi";
    cfg.server_socket_type = ZLINK_SOCKET_DEALER;
    cfg.client_socket_type = ZLINK_SOCKET_DEALER;
    cfg.server_router_echo = false;
    cfg.client_router_send = false;
    cfg.pubsub_mode = false;
    cfg.one_way_latency = false;
    cfg.server_has_routing_id = false;
    cfg.server_routing_id = "SERVER";

    if (!pattern)
        return cfg;

    if (std::strcmp (pattern, "MULTI_DEALER_DEALER") == 0) {
        cfg.token = "dealer_dealer";
        return cfg;
    }

    if (std::strcmp (pattern, "MULTI_DEALER_ROUTER") == 0) {
        cfg.token = "dealer_router";
        cfg.server_socket_type = ZLINK_SOCKET_ROUTER;
        cfg.server_router_echo = true;
        return cfg;
    }

    if (std::strcmp (pattern, "MULTI_ROUTER_ROUTER") == 0) {
        cfg.token = "router_router";
        cfg.server_socket_type = ZLINK_SOCKET_ROUTER;
        cfg.client_socket_type = ZLINK_SOCKET_ROUTER;
        cfg.server_router_echo = true;
        cfg.client_router_send = true;
        cfg.server_has_routing_id = true;
        cfg.server_routing_id = "SERVER";
        return cfg;
    }

    if (std::strcmp (pattern, "MULTI_PUBSUB") == 0) {
        cfg.token = "pubsub";
        cfg.server_socket_type = ZLINK_SOCKET_PUB;
        cfg.client_socket_type = ZLINK_SOCKET_SUB;
        cfg.pubsub_mode = true;
        return cfg;
    }

    if (std::strcmp (pattern, "MULTI_GATEWAY") == 0) {
        cfg.token = "gateway";
        cfg.one_way_latency = true;
        return cfg;
    }

    if (std::strcmp (pattern, "MULTI_SPOT") == 0) {
        cfg.token = "spot";
        cfg.one_way_latency = true;
        return cfg;
    }

    if (std::strcmp (pattern, "MULTI_STREAM") == 0) {
        cfg.token = "stream";
        return cfg;
    }

    if (std::strcmp (pattern, "MULTI_STREAM_CALLBACK") == 0) {
        cfg.token = "stream_callback";
        return cfg;
    }

    if (std::strcmp (pattern, "MULTI_STREAM_LEN32BE") == 0) {
        cfg.token = "stream_len32be";
        return cfg;
    }

    return cfg;
}

#endif
