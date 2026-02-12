/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ROUTER_ROUTER_SCENARIO_CORE_HPP_INCLUDED
#define ROUTER_ROUTER_SCENARIO_CORE_HPP_INCLUDED

#include <stddef.h>

namespace router_router_scenario {

struct SsConfig
{
    size_t size;
    int duration_sec;
    int ccu;
    int inflight;
    int sender_threads;
    bool self_connect;
    bool reconnect_enabled;
    int reconnect_interval_ms;
    int reconnect_down_ms;
    int play_port;
    int api_port;
};

struct RuntimeSnapshot
{
    long req_sent;
    long api_recv;
    long reply_recv;
    long reconnect_cycles;
    long play_would_block;
    long play_host_unreachable;
    long play_other;
    long api_would_block;
    long api_host_unreachable;
    long api_other;
};

struct Lz01SizeResult
{
    size_t size;
    int iterations;
    bool route_ready;
    long sent;
    long recv;
    long recv_err;
    long send_would_block;
    long send_host_unreachable;
    long send_other;
    long event_disconnected;
    long event_connect_retried;
    double elapsed_sec;
    double throughput_msg_s;
    bool pass;
};

struct SsResult
{
    bool ready;
    RuntimeSnapshot counters;
    long event_connection_ready;
    long event_connect_retried;
    long event_disconnected;
    long event_handshake_failed;
    double elapsed_sec;
    double throughput_msg_s;
};

Lz01SizeResult run_lz01_size (size_t size, int iterations, int port);
bool run_ss_round (const SsConfig &cfg, SsResult &out);

bool pass_lz02_like (const SsResult &r);
bool pass_lz04 (const SsResult &r);
bool pass_lz05 (const SsResult &r);

} // namespace router_router_scenario

#endif
