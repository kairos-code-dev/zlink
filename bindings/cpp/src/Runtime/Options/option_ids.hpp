/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

namespace zlink::detail
{

enum class socket_option_id : int
{
    linger = 12298,
    reconnect_ivl = 12299,
    backlog = 12300,
    reconnect_ivl_max = 12301,
    maxmsgsize = 12302,
    sndhwm = 12303,
    rcvhwm = 12304,
    rcvtimeo = 12306,
    sndtimeo = 12307,
    last_endpoint = 12308,
    tcp_keepalive = 12309,
    tcp_nodelay = 12337,
    immediate = 12313,
    ipv6 = 12314,
    submit_retry_mode = 12343,
    submit_retry_timeout = 12344,
    submit_retry_attempts = 12345,
    heartbeat_ivl = 12321,
    heartbeat_ttl = 12322,
    heartbeat_timeout = 12323,
    connect_timeout = 12324,
    rid_duplicate_policy = 12339
};

enum class router_option_id : int
{
    mandatory = 12545,
    probe = 12547,
    connect_routing_id = 12548,
    request_timeout_ms = 12549,
    weight = 12550
};

enum class dealer_option_id : int
{
    probe = 12801,
    request_timeout_ms = 12802,
    weight = 12803
};

enum class pub_option_id : int
{
    verbose = 13057,
    verboser = 13058,
    manual = 13059,
    manual_last_value = 13060,
    nodrop = 13061,
    welcome_msg = 13062,
    topics_count = 13063,
    approve_subscribe = 13064,
    reject_subscribe = 13065
};

enum class sub_option_id : int
{
    topics_count = 13312
};

enum class stream_option_id : int
{
    notify = 13569
};

void clear_dealer_option_store (void *handle_) noexcept;

} // namespace zlink::detail
