#ifndef PERF_SINGLE_COMMON_HPP
#define PERF_SINGLE_COMMON_HPP

#include "perf_single_metric_header.hpp"
#include "perf_single_tls.hpp"

#include <zlink.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace perf {
namespace single {

static const size_t MAX_SOCKET_STRING = 256;
static const int SETTLE_TIME_MS = 100;

struct latency_stats_t {
    latency_stats_t () : mean_us (0.0), p95_us (0.0), p99_us (0.0) {}

    double mean_us;
    double p95_us;
    double p99_us;
};

struct queue_stats_t {
    queue_stats_t ()
        : snd_pending_max (0.0),
          rcv_pending_max (0.0),
          rcv_pending_end (0.0),
          has_snd_pending (false),
          has_rcv_pending (false)
    {
    }

    double snd_pending_max;
    double rcv_pending_max;
    double rcv_pending_end;
    bool has_snd_pending;
    bool has_rcv_pending;
};

class latency_stats_builder_t
{
  public:
    explicit latency_stats_builder_t (size_t sample_cap_ = 200000);
    void add (double latency_us_);
    unsigned long long count () const;
    latency_stats_t snapshot ();

  private:
    static double percentile_from_sorted (const std::vector<double> &sorted_,
                                          double q_);
    unsigned long long next_rand_u64 ();

    size_t _sample_cap;
    unsigned long long _count;
    double _sum_us;
    unsigned long long _rng_state;
    std::vector<double> _samples;
};

class ctx_guard_t
{
  public:
    ctx_guard_t ();
    ~ctx_guard_t ();

    zlink::context_t &ctx () { return _ctx; }
    bool valid () const { return _ctx.handle () != NULL; }

  private:
    zlink::context_t _ctx;
};

class socket_guard_t
{
  public:
    socket_guard_t ();
    socket_guard_t (ctx_guard_t &ctx_, zlink::socket_type type_);

    zlink::socket_t &sock () { return _sock; }
    bool valid () const { return _sock.handle () != NULL; }

  private:
    zlink::socket_t _sock;
};

// Reads positive integer env var; returns default when missing/invalid/non-positive.
int parse_positive_env (const char *name_, int default_value_);
int resolve_single_duration_seconds ();
size_t resolve_single_latency_sample_cap ();
int resolve_single_send_timeout_ms ();
int resolve_single_recv_timeout_ms ();
int resolve_single_pubsub_recv_timeout_ms ();
int resolve_single_socket_hwm (bool send_);
int resolve_single_queue_sample_ms ();
int resolve_single_queue_sample_every_msgs ();
int resolve_bench_count (const char *env_name, int default_value);

bool bench_debug_enabled ();

// Applies shared benchmark context options (io_threads/max_sockets).
void apply_ctx_options (zlink::context_t &ctx_);
bool set_sockopt_int (zlink::socket_t &socket_,
                      zlink::socket_option_key_t<int> option_,
                      int value_,
                      const char *name_);
void apply_single_hwm (zlink::socket_t &socket_);
// Applies linger/send/recv timeout defaults for benchmark sockets.
void apply_single_benchmark_socket_options (zlink::socket_t &socket_,
                                            const std::string &transport_);

// Creates wildcard endpoint string for a transport/id pair.
std::string make_endpoint (const std::string &transport,
                           const std::string &id);
std::string make_fixed_endpoint (const std::string &transport, int port);
// Binds socket and returns normalized concrete endpoint (127.0.0.1 host form).
std::string bind_and_resolve_endpoint (zlink::socket_t &socket_,
                                       const std::string &transport,
                                       const std::string &id);

bool transport_available (const std::string &transport);
void settle ();
bool connect_checked (zlink::socket_t &socket_, const std::string &endpoint);
// Binds first socket and connects second socket to resolved endpoint.
bool setup_connected_pair (zlink::socket_t &bind_socket_,
                           zlink::socket_t &connect_socket_,
                           const std::string &transport_,
                           const std::string &id_);

void print_result (const std::string &lib_type,
                   const std::string &pattern,
                   const std::string &transport,
                   size_t size,
                   double throughput,
                   double latency,
                   double latency_p95,
                   double latency_p99);

void print_queue_metrics (const std::string &lib_type,
                          const std::string &pattern,
                          const std::string &transport,
                          size_t size,
                          const queue_stats_t &queue_stats);

void print_result (const std::string &lib_type,
                   const std::string &pattern,
                   const std::string &transport,
                   size_t size,
                   double throughput,
                   double latency,
                   double latency_p95,
                   double latency_p99,
                   const queue_stats_t &queue_stats);

void print_fail_result (const std::string &lib_type,
                        const std::string &pattern,
                        const std::string &transport,
                        size_t size);

class queue_probe_t
{
  public:
    queue_probe_t (zlink::socket_t *send_socket_, zlink::socket_t *recv_socket_);

    void sample_send_if_due ();
    void sample_recv_if_due ();
    void force_sample_send ();
    void force_sample_recv ();

    queue_stats_t snapshot () const;

  private:
    static unsigned long long resolve_sample_interval_ns ();
    static unsigned int resolve_sample_every_msgs ();
    static unsigned long long now_ns ();
    static unsigned long long peer_activity_score (const zlink_peer_info_t &info_);
    static bool read_first_peer_info (zlink::socket_t *socket_,
                                      zlink_peer_info_t *info_);

    void maybe_sample_send (bool force_);
    void maybe_sample_recv (bool force_);

    zlink::socket_t *_send_socket;
    zlink::socket_t *_recv_socket;
    unsigned long long _sample_interval_ns;
    unsigned int _sample_every_msgs;
    unsigned long long _send_last_sample_ns;
    unsigned long long _recv_last_sample_ns;
    unsigned int _send_msgs_since_sample;
    unsigned int _recv_msgs_since_sample;
    unsigned long long _snd_pending_max;
    unsigned long long _rcv_pending_max;
    unsigned long long _rcv_pending_end;
    bool _snd_seen;
    bool _rcv_seen;
};

queue_stats_t sample_queue_stats (queue_probe_t *queue_probe_);
void print_fail_result (const std::string &lib_type,
                        const std::string &pattern,
                        const std::string &transport,
                        size_t size,
                        queue_probe_t *queue_probe_);

} // namespace single
} // namespace perf

#endif
