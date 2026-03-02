#include "../common/perf_multi_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

static const char *k_pattern = "MULTI_DEALER_DEALER";
static const char *k_token = "dealer_dealer";
static const int k_server_socket_type = ZLINK_DEALER;

static std::atomic<bool> g_stop_requested (false);

inline void on_signal (int)
{
    g_stop_requested.store (true, std::memory_order_release);
}

inline void install_signal_handlers ()
{
    std::signal (SIGINT, on_signal);
#if defined(SIGTERM)
    std::signal (SIGTERM, on_signal);
#endif
}

inline bool is_supported_transport (const std::string &transport)
{
    return transport == "tcp" || transport == "tls" || transport == "ws"
           || transport == "wss";
}

inline std::string bind_server_endpoint (void *server,
                                         const std::string &transport,
                                         const std::string &token)
{
    const int bind_port =
      resolve_multi_int_env ("PERF_MULTI_SERVER_BIND_PORT", 0, 0);
    if (bind_port <= 0) {
        std::string endpoint_any = make_endpoint (transport, token);
        if (endpoint_any.empty ()) {
            std::cerr << "No endpoint available for transport " << transport
                      << std::endl;
            return std::string ();
        }
        if (zlink_bind (server, endpoint_any.c_str ()) != 0) {
            std::cerr << "bind failed for " << endpoint_any << ": "
                      << zlink_strerror (zlink_errno ()) << std::endl;
            return std::string ();
        }

        char last_endpoint[MAX_SOCKET_STRING] = "";
        size_t size = sizeof (last_endpoint);
        if (zlink_getsockopt (server, ZLINK_LAST_ENDPOINT, last_endpoint, &size)
            == 0) {
            endpoint_any.assign (last_endpoint);
            const std::string any_v4 = "://0.0.0.0:";
            const std::string any_v6 = "://[::]:";
            size_t pos = endpoint_any.find (any_v4);
            if (pos != std::string::npos) {
                endpoint_any.replace (pos, any_v4.size (), "://127.0.0.1:");
            } else {
                pos = endpoint_any.find (any_v6);
                if (pos != std::string::npos)
                    endpoint_any.replace (
                      pos, any_v6.size (), "://127.0.0.1:");
            }
        }

        apply_debug_timeouts (server, transport);
        return endpoint_any;
    }

    std::string endpoint = make_fixed_endpoint (transport, bind_port);
    if (zlink_bind (server, endpoint.c_str ()) != 0) {
        std::cerr << "bind failed for " << endpoint << ": "
                  << zlink_strerror (zlink_errno ()) << std::endl;
        return std::string ();
    }

    char last_endpoint[MAX_SOCKET_STRING] = "";
    size_t size = sizeof (last_endpoint);
    if (zlink_getsockopt (server, ZLINK_LAST_ENDPOINT, last_endpoint, &size) == 0)
        endpoint.assign (last_endpoint);
    apply_debug_timeouts (server, transport);
    return endpoint;
}

inline unsigned long long wallclock_now_us ()
{
    return static_cast<unsigned long long> (
      std::chrono::duration_cast<std::chrono::microseconds> (
        std::chrono::system_clock::now ().time_since_epoch ())
        .count ());
}

enum recv_result_t
{
    recv_ok = 0,
    recv_none = 1,
    recv_fatal = 2
};

inline recv_result_t receive_one_message (
  void *server,
  int flags,
  size_t expected_size,
  bool count_message,
  bool collect_latency,
  long *message_count,
  double *lat_sum,
  long *lat_count,
  bench_latency_sampler_t *lat_samples)
{
    if (!server)
        return recv_fatal;

    zlink_msg_t msg;
    if (zlink_msg_init (&msg) != 0)
        return recv_fatal;

    const int rc = zlink_msg_recv (&msg, server, flags);
    if (rc < 0) {
        const int err = zlink_errno ();
        zlink_msg_close (&msg);
        if (err == EAGAIN || err == EINTR || err == ETIMEDOUT)
            return recv_none;
        return recv_fatal;
    }

    const size_t msg_size = zlink_msg_size (&msg);
    const bool size_match = expected_size == 0 || msg_size == expected_size;

    if (size_match && count_message && message_count)
        (*message_count)++;

    if (size_match && collect_latency && lat_sum && lat_count
        && msg_size >= sizeof (unsigned long long)) {
        unsigned long long sent_us = 0;
        std::memcpy (&sent_us, zlink_msg_data (&msg), sizeof (sent_us));
        const unsigned long long now_us = wallclock_now_us ();
        if (now_us >= sent_us) {
            const double sample_us = static_cast<double> (now_us - sent_us);
            *lat_sum += sample_us;
            (*lat_count)++;
            if (lat_samples)
                lat_samples->add (sample_us);
        }
    }

    bool more = zlink_msg_more (&msg) != 0;
    zlink_msg_close (&msg);

    while (more) {
        zlink_msg_t next;
        if (zlink_msg_init (&next) != 0)
            return recv_fatal;

        int next_rc = zlink_msg_recv (&next, server, 0);
        while (next_rc < 0 && zlink_errno () == EINTR)
            next_rc = zlink_msg_recv (&next, server, 0);
        if (next_rc < 0) {
            zlink_msg_close (&next);
            return recv_fatal;
        }

        more = zlink_msg_more (&next) != 0;
        zlink_msg_close (&next);
    }

    return recv_ok;
}

inline bool drain_non_blocking_messages (
  void *server,
  size_t expected_size,
  bool count_message,
  bool collect_latency,
  long *message_count,
  double *lat_sum,
  long *lat_count,
  bench_latency_sampler_t *lat_samples)
{
    while (!g_stop_requested.load (std::memory_order_acquire)) {
        const recv_result_t status = receive_one_message (
          server,
          ZLINK_DONTWAIT,
          expected_size,
          count_message,
          collect_latency,
          message_count,
          lat_sum,
          lat_count,
          lat_samples);
        if (status == recv_none)
            break;
        if (status == recv_fatal)
            return false;
    }
    return true;
}

inline bool run_receive_window (
  void *server,
  size_t expected_size,
  double duration_seconds,
  bool count_message,
  bool collect_latency,
  long *message_count,
  double *lat_sum,
  long *lat_count,
  bench_latency_sampler_t *lat_samples)
{
    if (!server)
        return false;
    if (duration_seconds <= 0.0)
        return true;

    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::duration_cast<std::chrono::steady_clock::duration> (
        std::chrono::duration<double> (duration_seconds));

    while (!g_stop_requested.load (std::memory_order_acquire)
           && std::chrono::steady_clock::now () < deadline) {
        const recv_result_t status = receive_one_message (
          server,
          0,
          expected_size,
          count_message,
          collect_latency,
          message_count,
          lat_sum,
          lat_count,
          lat_samples);
        if (status == recv_none)
            continue;
        if (status == recv_fatal)
            return false;

        if (!drain_non_blocking_messages (
              server,
              expected_size,
              count_message,
              collect_latency,
              message_count,
              lat_sum,
              lat_count,
              lat_samples)) {
            return false;
        }
    }

    return true;
}

inline bool run_idle_stage (
  void *server,
  size_t expected_size,
  double duration_seconds)
{
    return run_receive_window (
      server,
      expected_size,
      duration_seconds,
      false,
      false,
      NULL,
      NULL,
      NULL,
      NULL);
}

inline bool run_one_size_benchmark (
  void *server,
  const multi_bench_settings_t &settings,
  size_t msg_size,
  bool has_next_size,
  const std::string &lib_name,
  const std::string &transport)
{
    const double warmup_s =
      static_cast<double> (std::max (0, settings.warmup_seconds));
    const double settle_s =
      static_cast<double> (std::max (0, settings.settle_ms)) / 1000.0;
    const double throughput_s =
      static_cast<double> (std::max (1, settings.duration_seconds));
    const double latency_s =
      static_cast<double> (std::max (1, settings.duration_seconds));
    const double drain_s =
      static_cast<double> (std::max (0, settings.drain_ms)) / 1000.0;
    const double transition_s = has_next_size
                                  ? static_cast<double> (
                                      std::max (0, settings.size_transition_drain_ms))
                                      / 1000.0
                                  : 0.0;

    if (!run_idle_stage (server, msg_size, warmup_s))
        return false;
    if (!run_idle_stage (server, msg_size, settle_s))
        return false;

    long throughput_count = 0;
    if (!run_receive_window (
          server,
          msg_size,
          throughput_s,
          true,
          false,
          &throughput_count,
          NULL,
          NULL,
          NULL)) {
        return false;
    }

    if (throughput_count <= 0)
        return false;

    if (!run_idle_stage (server, msg_size, settle_s))
        return false;

    double lat_sum = 0.0;
    long lat_count = 0;
    bench_latency_sampler_t lat_samples;
    if (!run_receive_window (
          server,
          msg_size,
          latency_s,
          false,
          true,
          NULL,
          &lat_sum,
          &lat_count,
          &lat_samples)) {
        return false;
    }

    if (lat_count <= 0)
        return false;

    bench_latency_stats_t latency = lat_samples.snapshot ();
    if (latency.mean_us <= 0.0)
        latency.mean_us = lat_sum / static_cast<double> (lat_count);
    if (latency.p95_us <= 0.0)
        latency.p95_us = latency.mean_us;
    if (latency.p99_us <= 0.0)
        latency.p99_us = latency.p95_us;
    if (latency.p95_us < latency.mean_us)
        latency.p95_us = latency.mean_us;
    if (latency.p99_us < latency.p95_us)
        latency.p99_us = latency.p95_us;

    const double throughput =
      static_cast<double> (throughput_count)
      / static_cast<double> (std::max (1, settings.duration_seconds));

    print_result (
      lib_name,
      k_pattern,
      transport,
      msg_size,
      throughput,
      latency.mean_us,
      latency.p95_us,
      latency.p99_us);

    const server_queue_stats_t queue_stats =
      sample_server_queue_stats (server, server);
    print_server_queue_metrics (
      lib_name,
      k_pattern,
      transport,
      msg_size,
      queue_stats);

    if (!run_idle_stage (server, msg_size, drain_s))
        return false;
    if (transition_s > 0.0 && !run_idle_stage (server, msg_size, transition_s))
        return false;

    return true;
}

inline void print_server_resource_metrics (
  const std::string &lib_name,
  const std::string &transport,
  const std::vector<size_t> &sizes,
  const bench_multi_resource_metrics_t &metrics)
{
    for (size_t i = 0; i < sizes.size (); ++i) {
        if (metrics.has_cpu_pct) {
            std::cout << "RESULT," << lib_name << "," << k_pattern << ","
                      << transport << "," << sizes[i]
                      << ",server_cpu_pct," << std::fixed
                      << std::setprecision (2) << metrics.cpu_pct << std::endl;
        }
        if (metrics.has_mem_mb) {
            std::cout << "RESULT," << lib_name << "," << k_pattern << ","
                      << transport << "," << sizes[i]
                      << ",server_mem_mb," << std::fixed
                      << std::setprecision (2) << metrics.mem_mb << std::endl;
        }
    }
}

inline int run_server_benchmark (const std::string &lib_name,
                                 const std::string &transport)
{
    set_perf_multi_pattern_env (k_pattern);

    if (!is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << ","
                  << transport << std::endl;
        return 0;
    }

    if (!transport_available (transport)) {
        std::cerr << "transport unavailable: " << transport << std::endl;
        return 1;
    }

    ctx_guard_t ctx;
    if (!ctx.valid ())
        return 1;

    void *server = zlink_socket (ctx.get (), k_server_socket_type);
    if (!server)
        return 1;

    const multi_bench_settings_t settings = resolve_multi_bench_settings ();
    apply_benchmark_socket_options (server, settings.hwm, transport);

    if (!setup_tls_server (server, transport)) {
        zlink_close (server);
        return 1;
    }

    connect_monitor_t server_monitor;
    if (!open_connect_monitor (server, server_monitor)) {
        zlink_close (server);
        return 1;
    }

    const std::string endpoint = bind_server_endpoint (
      server,
      transport,
      lib_name + std::string ("_") + k_token + "_server");
    if (endpoint.empty ()) {
        close_connect_monitor (server_monitor);
        zlink_close (server);
        return 1;
    }

    g_stop_requested.store (false, std::memory_order_release);
    install_signal_handlers ();

    std::thread stdin_watcher ([] () {
        std::string line;
        while (std::getline (std::cin, line)) {
            if (line == "STOP" || line == "QUIT") {
                g_stop_requested.store (true, std::memory_order_release);
                return;
            }
        }
        g_stop_requested.store (true, std::memory_order_release);
    });
    stdin_watcher.detach ();

    std::vector<size_t> sizes = resolve_bench_msg_sizes (64);
    if (sizes.empty ())
        sizes.push_back (64);

    std::cout << "READY," << endpoint << std::endl;

    if (!wait_connect_ready_count (
          server_monitor,
          settings.clients,
          settings.connect_ready_timeout_ms)) {
        close_connect_monitor (server_monitor);
        zlink_close (server);
        return 1;
    }

    const bench_multi_cpu_sample_t sample_start = bench_multi_capture_cpu_sample ();

    bool ok = true;
    for (size_t si = 0; si < sizes.size (); ++si) {
        if (g_stop_requested.load (std::memory_order_acquire)) {
            ok = false;
            break;
        }
        const bool has_next = (si + 1) < sizes.size ();
        if (!run_one_size_benchmark (
              server,
              settings,
              sizes[si],
              has_next,
              lib_name,
              transport)) {
            ok = false;
            break;
        }
    }

    const bench_multi_resource_metrics_t metrics =
      bench_multi_finish_resource_probe (sample_start);
    print_server_resource_metrics (lib_name, transport, sizes, metrics);

    close_connect_monitor (server_monitor);
    zlink_close (server);

    return ok ? 0 : 1;
}

} // namespace

int main (int argc, char **argv)
{
    if (argc < 3)
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    return run_server_benchmark (lib_name, transport);
}
