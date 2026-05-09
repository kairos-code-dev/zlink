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

typedef ::perf::socket_t perf_socket_t;

typedef ::perf::latency_sampler_stats_t latency_stats_t;

typedef ::perf::latency_sampler_t latency_stats_builder_t;

class ctx_guard_t
{
  public:
    ctx_guard_t ();
    ~ctx_guard_t ();

    zlink::context_t &ctx () { return _ctx; }
    operator zlink::context_t &() { return _ctx; }
    bool valid () const { return _ctx.valid (); }

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
zlink::auto_hwm_profile resolve_single_ctx_auto_hwm_profile ();
bool single_manual_socket_overrides_enabled ();

bool bench_debug_enabled ();

// Applies shared benchmark context options (io_threads/max_sockets).
void apply_ctx_options (zlink::context_t &ctx_);
bool set_sockopt_int (perf_socket_t &socket_,
                      zlink::compat::options::socket_option_key_t<int> option_,
                      int value_,
                      const char *name_);
template<typename SocketLike>
bool set_sockopt_int (SocketLike &socket_,
                      zlink::compat::options::socket_option_key_t<int> option_,
                      int value_,
                      const char *name_)
{
    try {
        zlink::common_socket_options_t options = socket_.options ();
        switch (option_.option) {
        case zlink::compat::options::socket_option::linger:
            options.linger (std::chrono::milliseconds (value_));
            return true;
        case zlink::compat::options::socket_option::sndhwm:
            options.send_hwm (zlink::message_count_t::value (value_));
            return true;
        case zlink::compat::options::socket_option::rcvhwm:
            options.recv_hwm (zlink::message_count_t::value (value_));
            return true;
        case zlink::compat::options::socket_option::sndtimeo:
            options.send_timeout (std::chrono::milliseconds (value_));
            return true;
        case zlink::compat::options::socket_option::rcvtimeo:
            options.recv_timeout (std::chrono::milliseconds (value_));
            return true;
        case zlink::compat::options::socket_option::tcp_nodelay:
            options.tcp_no_delay (value_ != 0);
            return true;
        default:
            errno = EOPNOTSUPP;
            if (bench_debug_enabled ()) {
                std::cerr << "setsockopt(" << (name_ ? name_ : "?")
                          << ") failed: unsupported public option" << std::endl;
            }
            return false;
        }
    }
    catch (const zlink::zlink_error_t &err) {
        errno = err.internal_errno ();
        if (bench_debug_enabled ()) {
            std::cerr << "setsockopt(" << (name_ ? name_ : "?")
                      << ") failed: " << err.what () << std::endl;
        }
        return false;
    }
}
void apply_single_hwm (perf_socket_t &socket_);
bool apply_single_auto_hwm_msg_unit (perf_socket_t &socket_, size_t msg_size_);
bool recalculate_single_auto_hwm (ctx_guard_t &ctx_);
namespace detail {
template<typename SubjectLike>
auto apply_auto_hwm_msg_unit_impl (SubjectLike &subject_,
                                   zlink::byte_size_t value_,
                                   int)
  -> decltype (subject_.options ().auto_hwm_msg_unit_bytes (value_), bool ())
{
    subject_.options ().auto_hwm_msg_unit_bytes (value_);
    return true;
}

template<typename SubjectLike>
auto apply_auto_hwm_msg_unit_impl (SubjectLike &subject_,
                                   zlink::byte_size_t value_,
                                   long)
  -> decltype (subject_.auto_hwm_msg_unit_bytes (value_), bool ())
{
    subject_.auto_hwm_msg_unit_bytes (value_);
    return true;
}

template<typename SubjectLike>
bool apply_auto_hwm_msg_unit_impl (SubjectLike &,
                                   zlink::byte_size_t,
                                   ...)
{
    return true;
}
} // namespace detail

template<typename SocketLike>
void apply_single_hwm (SocketLike &socket_)
{
    if (!single_manual_socket_overrides_enabled ())
        return;
    const int sndhwm = resolve_single_socket_hwm (true);
    const int rcvhwm = resolve_single_socket_hwm (false);
    (void) set_sockopt_int (
      socket_, zlink::compat::options::socket_options::sndhwm, sndhwm, "sndhwm");
    (void) set_sockopt_int (
      socket_, zlink::compat::options::socket_options::rcvhwm, rcvhwm, "rcvhwm");
}
template<typename SocketLike>
bool apply_single_auto_hwm_msg_unit (SocketLike &socket_, size_t msg_size_)
{
    if (msg_size_ == 0)
        return true;
    try {
        detail::apply_auto_hwm_msg_unit_impl (
          socket_,
          zlink::byte_size_t::bytes (static_cast<int64_t> (msg_size_)),
          0);
        return true;
    }
    catch (const zlink::config_error_t &err) {
        errno = err.internal_errno ();
        return false;
    }
}
// Applies linger/send/recv timeout defaults for benchmark sockets.
void apply_single_benchmark_socket_options (perf_socket_t &socket_,
                                            const std::string &transport_);
template<typename SocketLike>
void apply_single_benchmark_socket_options (SocketLike &socket_,
                                            const std::string &transport_)
{
    if (transport_ == "pgm" || transport_ == "epgm")
        return;

    const int linger_ms = 0;
    const int sndtimeo_ms = resolve_single_send_timeout_ms ();
    const int rcvtimeo_ms = resolve_single_recv_timeout_ms ();
    (void) set_sockopt_int (
      socket_, zlink::compat::options::socket_options::linger, linger_ms, "linger");
    (void) set_sockopt_int (
      socket_, zlink::compat::options::socket_options::sndtimeo, sndtimeo_ms, "sndtimeo");
    (void) set_sockopt_int (
      socket_, zlink::compat::options::socket_options::rcvtimeo, rcvtimeo_ms, "rcvtimeo");
}

// Creates wildcard endpoint string for a transport/id pair.
std::string make_endpoint (const std::string &transport,
                           const std::string &id);
std::string make_fixed_endpoint (const std::string &transport, int port);
// Binds socket and returns normalized concrete endpoint (127.0.0.1 host form).
std::string bind_and_resolve_endpoint (perf_socket_t &socket_,
                                       const std::string &transport,
                                       const std::string &id);
template<typename SocketLike>
std::string bind_and_resolve_endpoint (SocketLike &socket_,
                                       const std::string &transport,
                                       const std::string &id)
{
    std::string endpoint = make_endpoint (transport, id);
    if (endpoint.empty ())
        return std::string ();
    try {
        socket_.bind (endpoint);
    }
    catch (const zlink::zlink_error_t &) {
        return std::string ();
    }

    if (transport != "inproc") {
        endpoint = socket_.options ().last_endpoint ();

        const std::string any_v4 = "://0.0.0.0:";
        const std::string any_v6 = "://[::]:";
        size_t pos = endpoint.find (any_v4);
        if (pos != std::string::npos) {
            endpoint.replace (pos, any_v4.size (), "://127.0.0.1:");
        } else {
            pos = endpoint.find (any_v6);
            if (pos != std::string::npos)
                endpoint.replace (pos, any_v6.size (), "://127.0.0.1:");
        }
    }

    return endpoint;
}

bool transport_available (const std::string &transport);
// Binds first socket and connects second socket to resolved endpoint.
bool setup_connected_pair (perf_socket_t &bind_socket_,
                           perf_socket_t &connect_socket_,
                           const std::string &transport_,
                           const std::string &id_);
template<typename BindSocketLike, typename ConnectSocketLike>
bool setup_connected_pair (BindSocketLike &bind_socket_,
                           ConnectSocketLike &connect_socket_,
                           const std::string &transport_,
                           const std::string &id_)
{
    if (!setup_tls_server (bind_socket_, transport_)
        || !setup_tls_client (connect_socket_, transport_)) {
        return false;
    }

    apply_single_hwm (bind_socket_);
    apply_single_hwm (connect_socket_);

    zlink::monitor_handle_t bind_monitor = zlink::monitor_handle_t::open (
      bind_socket_, zlink::monitor_event::connection_ready);
    zlink::monitor_handle_t connect_monitor = zlink::monitor_handle_t::open (
      connect_socket_, zlink::monitor_event::connection_ready);
    if (!bind_monitor.valid () || !connect_monitor.valid ())
        return false;

    const std::string endpoint =
      bind_and_resolve_endpoint (bind_socket_, transport_, id_);
    if (endpoint.empty ())
        return false;
    try {
        connect_socket_.connect (endpoint);
    }
    catch (const zlink::zlink_error_t &) {
        return false;
    }

    apply_single_benchmark_socket_options (bind_socket_, transport_);
    apply_single_benchmark_socket_options (connect_socket_, transport_);
    if (!wait_socket_monitor_event (
          bind_monitor,
          static_cast<uint64_t> (zlink::monitor_event::connection_ready),
          10000)
        || !wait_socket_monitor_event (
          connect_monitor,
          static_cast<uint64_t> (zlink::monitor_event::connection_ready),
          10000)) {
        return false;
    }
    return true;
}
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

typedef bool (*phase_send_fn_t) (void *userdata_,
                                 const void *data_,
                                 size_t size_);

#include "perf_single_report.hpp"

} // namespace single
} // namespace perf

#endif
