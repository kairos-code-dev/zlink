#ifndef PERF_SINGLE_COMMON_HPP
#define PERF_SINGLE_COMMON_HPP

#include "perf_single_metric_header.hpp"
#include "../../common/perf_latency_sampler.hpp"
#include "../../common/perf_monitor_wait.hpp"
#include "../../common/perf_socket_compat.hpp"
#include "../../common/perf_tls.hpp"

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

typedef zlink::socket_t perf_socket_t;

typedef ::perf::latency_sampler_stats_t latency_stats_t;

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
int resolve_single_pubsub_ready_settle_ms ();
int resolve_single_spot_ready_settle_ms ();
int resolve_single_socket_hwm (bool send_);

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
// Binds first socket and connects second socket to resolved endpoint.
bool setup_connected_pair (perf_socket_t &bind_socket_,
                           perf_socket_t &connect_socket_,
                           const std::string &transport_,
                           const std::string &id_);
// Migrated to unified perf::wait_socket_monitor_event in
// common/perf_monitor_wait.hpp.
using ::perf::wait_socket_monitor_event;

void print_result (const std::string &lib_type,
                   const std::string &pattern,
                   const std::string &transport,
                   size_t size,
                   double throughput,
                   double latency,
                   double latency_p95,
                   double latency_p99);

void print_fail_result (const std::string &lib_type,
                        const std::string &pattern,
                        const std::string &transport,
                        size_t size);

typedef bool (*phase_send_fn_t) (void *userdata_,
                                 const void *data_,
                                 size_t size_);

#include "perf_single_report.hpp"

} // namespace single
} // namespace perf

#endif
