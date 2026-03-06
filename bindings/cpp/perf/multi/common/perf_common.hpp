#ifndef PERF_MULTI_COMMON_HPP
#define PERF_MULTI_COMMON_HPP

#include "perf_common_multi.hpp"
#include "perf_metric_header.hpp"
#include "perf_tls.hpp"

#include <zlink.hpp>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace perf {
namespace multi {

static const char *k_stop_token = "__zlink_perf_stop__";

inline bool is_supported_transport (const std::string &transport)
{
    return transport == "tcp" || transport == "tls" || transport == "ws"
           || transport == "wss";
}

class ctx_guard_t
{
  public:
    ctx_guard_t () : _ctx (), _skip_term (false)
    {
        const int io_threads = parse_positive_env ("PERF_IO_THREADS", 0);
        if (io_threads > 0)
            (void) _ctx.set (zlink::context_option::io_threads, io_threads);

        const int max_sockets = parse_positive_env ("PERF_MAX_SOCKETS", 0);
        if (max_sockets > 0)
            (void) _ctx.set (zlink::context_option::max_sockets, max_sockets);

    }

    ~ctx_guard_t ()
    {
        if (_ctx.handle ())
            (void) _ctx.shutdown ();
        if (!_skip_term) {
            const int do_term = parse_positive_env ("PERF_CTX_TERM", 0);
            if (do_term > 0)
                force_term ();
        }
    }

    void force_term ()
    {
        if (!_ctx.handle ())
            return;
        (void) _ctx.shutdown ();
        _skip_term = true;
    }

    zlink::context_t &ctx () { return _ctx; }

  private:
    zlink::context_t _ctx;
    bool _skip_term;
};

class socket_guard_t
{
  public:
    socket_guard_t () : _sock () {}
    socket_guard_t (ctx_guard_t &ctx, zlink::socket_type type) : _sock (ctx.ctx (), type)
    {
    }

    zlink::socket_t &sock () { return _sock; }
    bool valid () const { return _sock.handle () != NULL; }

  private:
    zlink::socket_t _sock;
};

struct connect_monitor_t
{
    connect_monitor_t () : owner (NULL), monitor () {}

    zlink::socket_t *owner;
    std::unique_ptr<zlink::socket_t> monitor;
};

struct server_queue_stats_t
{
    server_queue_stats_t () : snd_pending_max (0.0), rcv_pending_max (0.0), rcv_pending_end (0.0)
    {
    }

    double snd_pending_max;
    double rcv_pending_max;
    double rcv_pending_end;
};

struct bench_latency_stats_t
{
    bench_latency_stats_t () : mean_us (0.0), p95_us (0.0), p99_us (0.0) {}

    double mean_us;
    double p95_us;
    double p99_us;
};

class bench_latency_sampler_t
{
  public:
    explicit bench_latency_sampler_t (size_t cap = 200000)
        : _cap (cap > 0 ? cap : 1), _count (0), _sum (0.0), _rng (0x9e3779b97f4a7c15ULL)
    {
        _samples.reserve (_cap);
    }

    void add (double latency_us)
    {
        const double sample = latency_us >= 0.0 ? latency_us : 0.0;
        ++_count;
        _sum += sample;

        if (_samples.size () < _cap) {
            _samples.push_back (sample);
            return;
        }

        const unsigned long long slot = next_rand () % _count;
        if (slot < static_cast<unsigned long long> (_cap))
            _samples[static_cast<size_t> (slot)] = sample;
    }

    unsigned long long count () const { return _count; }

    bench_latency_stats_t snapshot ()
    {
        bench_latency_stats_t out;
        if (_count == 0)
            return out;

        out.mean_us = _sum / static_cast<double> (_count);
        if (_samples.empty ()) {
            out.p95_us = out.mean_us;
            out.p99_us = out.mean_us;
            return out;
        }

        std::sort (_samples.begin (), _samples.end ());
        out.p95_us = percentile (_samples, 0.95);
        out.p99_us = percentile (_samples, 0.99);
        if (out.p95_us < out.mean_us)
            out.p95_us = out.mean_us;
        if (out.p99_us < out.p95_us)
            out.p99_us = out.p95_us;
        return out;
    }

  private:
    static double percentile (const std::vector<double> &v, double q)
    {
        if (v.empty ())
            return 0.0;
        if (q <= 0.0)
            return v.front ();
        if (q >= 1.0)
            return v.back ();

        const double pos = (v.size () - 1) * q;
        const size_t lo = static_cast<size_t> (pos);
        const size_t hi = lo + 1 < v.size () ? lo + 1 : lo;
        const double frac = pos - static_cast<double> (lo);
        return v[lo] + (v[hi] - v[lo]) * frac;
    }

    unsigned long long next_rand ()
    {
        unsigned long long x = _rng;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        _rng = x;
        return x;
    }

    size_t _cap;
    unsigned long long _count;
    double _sum;
    unsigned long long _rng;
    std::vector<double> _samples;
};

inline void apply_benchmark_hwm (zlink::socket_t &socket,
                                 int sndhwm,
                                 int rcvhwm)
{
    const int snd_value = sndhwm > 0 ? sndhwm : 1;
    const int rcv_value = rcvhwm > 0 ? rcvhwm : 1;
    (void) socket.set (zlink::socket_options::sndhwm, snd_value);
    (void) socket.set (zlink::socket_options::rcvhwm, rcv_value);
}

inline void apply_debug_timeouts (zlink::socket_t &socket,
                                  const std::string &)
{
    const multi_bench_settings_t settings = resolve_multi_bench_settings ();
    (void) socket.set (zlink::socket_options::sndtimeo, settings.sndtimeo_ms);
    (void) socket.set (zlink::socket_options::rcvtimeo, settings.rcvtimeo_ms);
    (void) socket.set (zlink::socket_options::linger, 0);
}

inline void apply_benchmark_socket_options (zlink::socket_t &socket,
                                            const multi_bench_settings_t &settings,
                                            const std::string &transport)
{
    // Bench sockets use explicit HWM + timeout/linger to keep run behavior stable.
    apply_benchmark_hwm (socket, settings.sndhwm, settings.rcvhwm);
    apply_debug_timeouts (socket, transport);
}

inline bool open_connect_monitor (zlink::socket_t &socket, connect_monitor_t &out)
{
    zlink::socket_t monitor = socket.monitor_open (zlink::monitor_event::connection_ready);
    if (!monitor.handle ())
        return false;

    const int monitor_hwm = resolve_multi_bench_settings ().monitor_hwm;
    (void) monitor.set (zlink::socket_options::sndhwm, monitor_hwm);
    (void) monitor.set (zlink::socket_options::rcvhwm, monitor_hwm);
    (void) monitor.set (zlink::socket_options::linger, 0);

    out.owner = &socket;
    out.monitor.reset (new zlink::socket_t (std::move (monitor)));
    return true;
}

inline bool wait_connect_ready (zlink::socket_t &, int)
{
    return true;
}

inline bool wait_connect_ready_count (zlink::socket_t &, size_t, int)
{
    return true;
}

inline void close_connect_monitor (connect_monitor_t &mon)
{
    mon.monitor.reset ();
    mon.owner = NULL;
}

inline std::string make_endpoint (const std::string &transport,
                                  const std::string &id,
                                  int fixed_port)
{
    if (transport == "inproc")
        return std::string ("inproc://") + id;
    if (transport == "ipc")
        return "ipc://*";

    const std::string host = "127.0.0.1";
    if (fixed_port > 0) {
        if (transport == "ws")
            return "ws://" + host + ":" + std::to_string (fixed_port);
        if (transport == "wss")
            return "wss://" + host + ":" + std::to_string (fixed_port);
        if (transport == "tls")
            return "tls://" + host + ":" + std::to_string (fixed_port);
        return "tcp://" + host + ":" + std::to_string (fixed_port);
    }

    if (transport == "ws")
        return "ws://" + host + ":*";
    if (transport == "wss")
        return "wss://" + host + ":*";
    if (transport == "tls")
        return "tls://" + host + ":*";
    return "tcp://" + host + ":*";
}

inline std::string normalize_endpoint_host (const std::string &endpoint)
{
    std::string out = endpoint;
    const std::string any_v4 = "://0.0.0.0:";
    const std::string any_v6 = "://[::]:";
    size_t pos = out.find (any_v4);
    if (pos != std::string::npos)
        out.replace (pos, any_v4.size (), "://127.0.0.1:");
    pos = out.find (any_v6);
    if (pos != std::string::npos)
        out.replace (pos, any_v6.size (), "://127.0.0.1:");
    return out;
}

inline std::string bind_and_resolve_endpoint (zlink::socket_t &socket,
                                              const std::string &transport,
                                              const std::string &id,
                                              int fixed_port)
{
    // For wildcard binds, normalize to loopback so clients always connect to 127.0.0.1.
    const std::string endpoint = make_endpoint (transport, id, fixed_port);
    if (socket.bind (endpoint) != 0)
        return std::string ();

    if (transport == "inproc")
        return endpoint;

    std::string last;
    if (socket.get (zlink::socket_options::last_endpoint, last) != 0)
        return std::string ();

    return normalize_endpoint_host (last);
}

inline void settle ()
{
    // Keep a short post-connect stabilization window before benchmark phases.
    std::this_thread::sleep_for (std::chrono::milliseconds (300));
}

inline bool is_stop_token (const void *data, size_t size)
{
    const size_t token_size = std::strlen (k_stop_token);
    if (size == token_size
        && std::memcmp (data, k_stop_token, token_size) == 0) {
        return true;
    }

    // stream client uses len32be framing: [4-byte len][payload].
    if (size >= 4 + token_size) {
        const unsigned char *bytes = static_cast<const unsigned char *> (data);
        const uint32_t len = (static_cast<uint32_t> (bytes[0]) << 24)
                             | (static_cast<uint32_t> (bytes[1]) << 16)
                             | (static_cast<uint32_t> (bytes[2]) << 8)
                             | static_cast<uint32_t> (bytes[3]);
        if (len == size - 4
            && len == token_size
            && std::memcmp (bytes + 4, k_stop_token, token_size) == 0) {
            return true;
        }
    }

    return false;
}

inline bool is_stop_token_message (const zlink::message_t &msg)
{
    return is_stop_token (msg.data (), msg.size ());
}

inline void print_result (const std::string &lib,
                          const std::string &pattern,
                          const std::string &transport,
                          size_t size,
                          double throughput,
                          double bandwidth,
                          double latency_us,
                          double p95_us,
                          double p99_us)
{
    std::cout << "RESULT," << lib << "," << pattern << "," << transport << "," << size
              << ",throughput," << std::fixed << std::setprecision (2) << throughput
              << std::endl;
    std::cout << "RESULT," << lib << "," << pattern << "," << transport << "," << size
              << ",bandwidth," << std::fixed << std::setprecision (2) << bandwidth
              << std::endl;
    std::cout << "RESULT," << lib << "," << pattern << "," << transport << "," << size
              << ",latency," << std::fixed << std::setprecision (2) << latency_us
              << std::endl;
    std::cout << "RESULT," << lib << "," << pattern << "," << transport << "," << size
              << ",latency_p95," << std::fixed << std::setprecision (2) << p95_us
              << std::endl;
    std::cout << "RESULT," << lib << "," << pattern << "," << transport << "," << size
              << ",latency_p99," << std::fixed << std::setprecision (2) << p99_us
              << std::endl;
}

inline void print_server_queue_metrics (const std::string &lib,
                                        const std::string &pattern,
                                        const std::string &transport,
                                        size_t size,
                                        const server_queue_stats_t &stats)
{
    std::cout << "RESULT," << lib << "," << pattern << "," << transport << "," << size
              << ",server_snd_pending_max," << std::fixed << std::setprecision (2)
              << stats.snd_pending_max << std::endl;
    std::cout << "RESULT," << lib << "," << pattern << "," << transport << "," << size
              << ",server_rcv_pending_max," << std::fixed << std::setprecision (2)
              << stats.rcv_pending_max << std::endl;
    std::cout << "RESULT," << lib << "," << pattern << "," << transport << "," << size
              << ",server_rcv_pending_end," << std::fixed << std::setprecision (2)
              << stats.rcv_pending_end << std::endl;
}

inline void print_cpu_mem_metrics (const std::string &,
                                   const std::string &,
                                   const std::string &,
                                   size_t,
                                   const std::string &,
                                   double)
{
    // run_policy_bench.py samples cpu/mem externally for cpp.
}

inline void print_ready (const std::string &endpoint)
{
    std::cout << "READY," << endpoint << std::endl;
    std::cout.flush ();
}

} // namespace multi
} // namespace perf

#endif
