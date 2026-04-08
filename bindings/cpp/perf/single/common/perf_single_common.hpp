#ifndef PERF_SINGLE_COMMON_HPP
#define PERF_SINGLE_COMMON_HPP

#include "perf_single_metric_header.hpp"
#include "../../common/perf_latency_sampler.hpp"
#include "../../common/perf_monitor_wait.hpp"
#include "../../common/perf_socket_compat.hpp"
#include "../../common/perf_tls.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace perf {
namespace single {

typedef zlink::socket_t perf_socket_t;

static const size_t MAX_SOCKET_STRING = 256;

// Migrated to unified perf::latency_stats_t / perf::latency_sampler_t.
typedef ::perf::latency_sampler_stats_t latency_stats_t;

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

typedef ::perf::latency_sampler_t latency_stats_builder_t;

class ctx_guard_t
{
  public:
    ctx_guard_t ();
    ~ctx_guard_t ();

    zlink::context_t &ctx () { return _ctx; }
    operator zlink::context_t &() { return _ctx; }
    bool valid () const { return _ctx.handle () != NULL; }

  private:
    zlink::context_t _ctx;
};

using ::perf::socket_guard_t;

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
bool set_sockopt_int (perf_socket_t &socket_,
                      zlink::socket_option_key_t<int> option_,
                      int value_,
                      const char *name_);
void apply_single_hwm (perf_socket_t &socket_);
// Applies linger/send/recv timeout defaults for benchmark sockets.
void apply_single_benchmark_socket_options (perf_socket_t &socket_,
                                            const std::string &transport_);

// Creates wildcard endpoint string for a transport/id pair.
std::string make_endpoint (const std::string &transport,
                           const std::string &id);
std::string make_fixed_endpoint (const std::string &transport, int port);
// Binds socket and returns normalized concrete endpoint (127.0.0.1 host form).
std::string bind_and_resolve_endpoint (perf_socket_t &socket_,
                                       const std::string &transport,
                                       const std::string &id);

bool transport_available (const std::string &transport);
bool connect_checked (perf_socket_t &socket_, const std::string &endpoint);
// Binds first socket and connects second socket to resolved endpoint.
bool setup_connected_pair (perf_socket_t &bind_socket_,
                           perf_socket_t &connect_socket_,
                           const std::string &transport_,
                           const std::string &id_);
// Migrated to unified perf::wait_socket_monitor_event /
// perf::wait_service_monitor_event in common/perf_monitor_wait.hpp.
using ::perf::wait_socket_monitor_event;
using ::perf::wait_service_monitor_event;

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
    queue_probe_t (perf_socket_t *send_socket_, perf_socket_t *recv_socket_);

    void sample_send_if_due ();
    void sample_recv_if_due ();
    void force_sample_send ();
    void force_sample_recv ();

    queue_stats_t snapshot () const;

  private:
    static unsigned long long resolve_sample_interval_ns ();
    static unsigned int resolve_sample_every_msgs ();
    static unsigned long long now_ns ();
    static bool read_snapshot (zlink::monitor_handle_t *monitor_,
                               zlink::monitor_snapshot_t *snapshot_);

    void maybe_sample_send (bool force_);
    void maybe_sample_recv (bool force_);

    perf_socket_t *_send_socket;
    perf_socket_t *_recv_socket;
    zlink::monitor_handle_t _send_monitor;
    zlink::monitor_handle_t _recv_monitor;
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

typedef bool (*phase_send_fn_t) (void *userdata_,
                                 const void *data_,
                                 size_t size_);

#include "perf_single_report.hpp"
#include "perf_single_callback_receiver.hpp"

} // namespace single
} // namespace perf

#endif
