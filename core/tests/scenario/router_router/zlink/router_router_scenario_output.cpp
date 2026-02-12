/* SPDX-License-Identifier: MPL-2.0 */

#include "router_router_scenario_output.hpp"

#include "router_router_scenario_core.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

using namespace router_router_scenario;

int process_id ()
{
#if defined(_WIN32)
    return _getpid ();
#else
    return getpid ();
#endif
}

bool has_arg (int argc, char **argv, const char *key)
{
    for (int i = 1; i < argc; ++i) {
        if (strcmp (argv[i], key) == 0)
            return true;
    }
    return false;
}

int arg_int (int argc, char **argv, const char *key, int fallback)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (strcmp (argv[i], key) == 0)
            return atoi (argv[i + 1]);
    }
    return fallback;
}

std::string arg_str (int argc,
                     char **argv,
                     const char *key,
                     const char *fallback)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (strcmp (argv[i], key) == 0)
            return std::string (argv[i + 1]);
    }
    return std::string (fallback);
}

void print_ss_result (const char *name, const SsConfig &cfg, const SsResult &r)
{
    const long outstanding = r.counters.req_sent - r.counters.reply_recv;
    printf ("[%s] size=%zu duration=%ds ccu=%d inflight=%d self=%d senders=%d\n",
            name, cfg.size, cfg.duration_sec, cfg.ccu, cfg.inflight,
            cfg.self_connect ? 1 : 0, cfg.sender_threads);
    printf ("  ready=%d req_sent=%ld api_recv=%ld reply_recv=%ld outstanding=%ld\n",
            r.ready ? 1 : 0, r.counters.req_sent, r.counters.api_recv,
            r.counters.reply_recv, outstanding);
    printf ("  play_send_err: would_block=%ld host_unreachable=%ld other=%ld\n",
            r.counters.play_would_block, r.counters.play_host_unreachable,
            r.counters.play_other);
    printf ("  api_send_err : would_block=%ld host_unreachable=%ld other=%ld\n",
            r.counters.api_would_block, r.counters.api_host_unreachable,
            r.counters.api_other);
    printf (
      "  monitor      : connection_ready=%ld connect_retried=%ld disconnected=%ld handshake_failed=%ld\n",
      r.event_connection_ready, r.event_connect_retried, r.event_disconnected,
      r.event_handshake_failed);
    printf ("  throughput   : %.0f msg/s (elapsed %.2fs)\n", r.throughput_msg_s,
            r.elapsed_sec);
}

void print_usage (const char *prog)
{
    printf ("Usage: %s [--scenario smoke|lz-01|lz-02|lz-03|lz-04|lz-05|all] [options]\n",
            prog);
    printf ("Options:\n");
    printf ("  --size N                Payload size in bytes (default 64)\n");
    printf ("  --duration N            Duration seconds for SS runs (default 10)\n");
    printf ("  --ccu N                 Header stage cardinality (default 50)\n");
    printf ("  --inflight N            Inflight cap (default 10)\n");
    printf ("  --senders N             Sender thread count (default 1)\n");
    printf ("  --self-connect 0|1      Enable self-connect on clients (default 0)\n");
    printf ("  --iterations N          Iterations for LZ-01 single-size mode\n");
    printf ("  --play-port N           Play server port (default 16100)\n");
    printf ("  --api-port N            Api server port (default 16201)\n");
    printf ("  --reconnect-ms N        Reconnect interval ms for LZ-05 (default 1000)\n");
    printf ("  --reconnect-down-ms N   Reconnect down window ms (default 200)\n");
}

int run_smoke (int argc, char **argv)
{
    SsConfig cfg;
    cfg.size = static_cast<size_t> (arg_int (argc, argv, "--size", 64));
    cfg.duration_sec = arg_int (argc, argv, "--duration", 2);
    cfg.ccu = arg_int (argc, argv, "--ccu", 10);
    cfg.inflight = arg_int (argc, argv, "--inflight", 10);
    cfg.sender_threads = arg_int (argc, argv, "--senders", 1);
    cfg.self_connect = arg_int (argc, argv, "--self-connect", 0) == 1;
    cfg.reconnect_enabled = false;
    cfg.reconnect_interval_ms = arg_int (argc, argv, "--reconnect-ms", 1000);
    cfg.reconnect_down_ms = arg_int (argc, argv, "--reconnect-down-ms", 200);
    cfg.play_port = arg_int (argc, argv, "--play-port", 16100);
    cfg.api_port = arg_int (argc, argv, "--api-port", 16201);

    SsResult r;
    run_ss_round (cfg, r);
    print_ss_result ("SMOKE", cfg, r);

    const bool pass = r.ready && r.counters.reply_recv > 0
                      && r.counters.play_host_unreachable == 0
                      && r.counters.api_host_unreachable == 0;
    printf ("[SMOKE] %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 2;
}

int run_lz01 (int argc, char **argv)
{
    const bool custom_size = has_arg (argc, argv, "--size");
    std::vector<size_t> sizes;
    if (custom_size) {
        sizes.push_back (static_cast<size_t> (arg_int (argc, argv, "--size", 64)));
    } else {
        sizes.push_back (64);
        sizes.push_back (1024);
        sizes.push_back (65536);
    }

    const int default_iters = arg_int (argc, argv, "--iterations", -1);
    const int port_seed = 18000 + (process_id () % 20000);
    bool all_pass = true;

    for (size_t i = 0; i < sizes.size (); ++i) {
        const size_t sz = sizes[i];
        int iters = default_iters;
        if (iters <= 0) {
            iters = (sz <= 64) ? 20000 : (sz <= 1024) ? 10000 : 2000;
        }
        const Lz01SizeResult r =
          run_lz01_size (sz, iters, port_seed + static_cast<int> (i));
        printf (
          "[LZ-01] size=%zu iterations=%d route_ready=%d sent=%ld recv=%ld recv_err=%ld send_err(host=%ld,other=%ld,would_block=%ld) events(disconnected=%ld,connect_retried=%ld) throughput=%.0f msg/s\n",
          r.size, r.iterations, r.route_ready ? 1 : 0, r.sent, r.recv, r.recv_err,
          r.send_host_unreachable, r.send_other, r.send_would_block,
          r.event_disconnected, r.event_connect_retried, r.throughput_msg_s);
        printf ("[LZ-01] size=%zu %s\n", r.size, r.pass ? "PASS" : "FAIL");
        if (!r.pass)
            all_pass = false;
    }
    return all_pass ? 0 : 2;
}

int run_lz02_or_lz03 (int argc, char **argv, bool self_connect)
{
    SsConfig cfg;
    cfg.size = static_cast<size_t> (arg_int (argc, argv, "--size", 64));
    cfg.duration_sec = arg_int (argc, argv, "--duration", 10);
    cfg.ccu = arg_int (argc, argv, "--ccu", 50);
    cfg.inflight = arg_int (argc, argv, "--inflight", 10);
    cfg.sender_threads = arg_int (argc, argv, "--senders", 1);
    cfg.self_connect = self_connect;
    cfg.reconnect_enabled = false;
    cfg.reconnect_interval_ms = arg_int (argc, argv, "--reconnect-ms", 1000);
    cfg.reconnect_down_ms = arg_int (argc, argv, "--reconnect-down-ms", 200);
    cfg.play_port = arg_int (argc, argv, "--play-port", 16100);
    cfg.api_port = arg_int (argc, argv, "--api-port", 16201);

    SsResult r;
    run_ss_round (cfg, r);
    print_ss_result (self_connect ? "LZ-03" : "LZ-02", cfg, r);
    const bool pass = pass_lz02_like (r);
    printf ("[%s] %s\n", self_connect ? "LZ-03" : "LZ-02",
            pass ? "PASS" : "FAIL");
    return pass ? 0 : 2;
}

int run_lz04 (int argc, char **argv)
{
    const bool single_size = has_arg (argc, argv, "--size");
    std::vector<size_t> sizes;
    if (single_size) {
        sizes.push_back (static_cast<size_t> (arg_int (argc, argv, "--size", 64)));
    } else {
        sizes.push_back (64);
        sizes.push_back (1024);
        sizes.push_back (65536);
    }

    const int duration = arg_int (argc, argv, "--duration", 10);
    const int ccu = arg_int (argc, argv, "--ccu", 200);
    const int inflight = arg_int (argc, argv, "--inflight", 200);
    const int senders = arg_int (argc, argv, "--senders", 4);
    const int play_port = arg_int (argc, argv, "--play-port", 16100);
    const int api_port = arg_int (argc, argv, "--api-port", 16201);

    bool all_pass = true;
    for (size_t i = 0; i < sizes.size (); ++i) {
        SsConfig cfg;
        cfg.size = sizes[i];
        cfg.duration_sec = duration;
        cfg.ccu = ccu;
        cfg.inflight = inflight;
        cfg.sender_threads = senders;
        cfg.self_connect = true;
        cfg.reconnect_enabled = false;
        cfg.reconnect_interval_ms = arg_int (argc, argv, "--reconnect-ms", 1000);
        cfg.reconnect_down_ms = arg_int (argc, argv, "--reconnect-down-ms", 200);
        cfg.play_port = play_port + static_cast<int> (i) * 2;
        cfg.api_port = api_port + static_cast<int> (i) * 2;

        SsResult r;
        run_ss_round (cfg, r);
        print_ss_result ("LZ-04", cfg, r);
        const bool pass = pass_lz04 (r);
        printf ("[LZ-04] size=%zu %s\n", cfg.size, pass ? "PASS" : "FAIL");
        if (!pass)
            all_pass = false;
    }
    return all_pass ? 0 : 2;
}

int run_lz05 (int argc, char **argv)
{
    SsConfig cfg;
    cfg.size = static_cast<size_t> (arg_int (argc, argv, "--size", 64));
    cfg.duration_sec = arg_int (argc, argv, "--duration", 15);
    cfg.ccu = arg_int (argc, argv, "--ccu", 50);
    cfg.inflight = arg_int (argc, argv, "--inflight", 20);
    cfg.sender_threads = arg_int (argc, argv, "--senders", 2);
    cfg.self_connect = arg_int (argc, argv, "--self-connect", 1) == 1;
    cfg.reconnect_enabled = true;
    cfg.reconnect_interval_ms = arg_int (argc, argv, "--reconnect-ms", 1000);
    cfg.reconnect_down_ms = arg_int (argc, argv, "--reconnect-down-ms", 200);
    cfg.play_port = arg_int (argc, argv, "--play-port", 16100);
    cfg.api_port = arg_int (argc, argv, "--api-port", 16201);

    SsResult r;
    run_ss_round (cfg, r);
    print_ss_result ("LZ-05", cfg, r);
    const bool pass = pass_lz05 (r);
    printf ("[LZ-05] %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 2;
}

int run_all (int argc, char **argv)
{
    int rc = 0;
    rc |= run_lz01 (argc, argv);
    rc |= run_lz02_or_lz03 (argc, argv, false);
    rc |= run_lz02_or_lz03 (argc, argv, true);
    rc |= run_lz04 (argc, argv);
    rc |= run_lz05 (argc, argv);
    return rc == 0 ? 0 : 2;
}

} // namespace

namespace router_router_scenario {

int run_cli (int argc, char **argv)
{
    const std::string scenario = arg_str (argc, argv, "--scenario", "smoke");

    if (scenario == "smoke")
        return run_smoke (argc, argv);
    if (scenario == "lz-01")
        return run_lz01 (argc, argv);
    if (scenario == "lz-02")
        return run_lz02_or_lz03 (argc, argv, false);
    if (scenario == "lz-03")
        return run_lz02_or_lz03 (argc, argv, true);
    if (scenario == "lz-04")
        return run_lz04 (argc, argv);
    if (scenario == "lz-05")
        return run_lz05 (argc, argv);
    if (scenario == "all")
        return run_all (argc, argv);

    print_usage (argv[0]);
    return 2;
}

} // namespace router_router_scenario
