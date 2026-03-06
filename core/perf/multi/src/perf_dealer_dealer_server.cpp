#include "../common/perf_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_metric_header.hpp"
#include "../../../bench/with_zmq/multi/common/bench_resource.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

static const char *k_pattern = "DEALER_DEALER";
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
    if (transport == "tcp" || transport == "tls" || transport == "ws"
        || transport == "wss")
        return true;
#if !defined(_WIN32)
    if (transport == "ipc")
        return true;
#endif
    return false;
}

inline std::string bind_server_endpoint (void *server,
                                         const std::string &transport,
                                         const std::string &token)
{
    const int bind_port =
      resolve_int_env ("PERF_SERVER_BIND_PORT", 0, 0);
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

enum recv_result_t
{
    recv_ok = 0,
    recv_none = 1,
    recv_fatal = 2
};

inline bool decode_and_match_header (const zlink_msg_t *msg,
                                     size_t expected_msg_size,
                                     uint32_t expected_run_id,
                                     perf_metric::phase_t expected_phase,
                                     perf_metric::header_t *header_out)
{
    if (!msg || !header_out)
        return false;

    if (!perf_metric::decode_payload_header (
          zlink_msg_data (const_cast<zlink_msg_t *> (msg)),
          zlink_msg_size (const_cast<zlink_msg_t *> (msg)),
          header_out)) {
        return false;
    }

    return header_out->magic == perf_metric::k_magic
           && header_out->run_id == expected_run_id
           && header_out->phase == static_cast<uint32_t> (expected_phase)
           && header_out->msg_size == static_cast<uint32_t> (expected_msg_size);
}

inline recv_result_t receive_one_message (
  void *server,
  int flags,
  size_t expected_msg_size,
  uint32_t expected_run_id,
  perf_metric::phase_t expected_phase,
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

    perf_metric::header_t header;
    const bool matched = decode_and_match_header (
      &msg,
      expected_msg_size,
      expected_run_id,
      expected_phase,
      &header);

    if (matched && count_message && message_count)
        (*message_count)++;

    if (matched && collect_latency && lat_sum && lat_count) {
        const uint64_t now_us = perf_metric::now_us ();
        if (header.sent_ts_us > 0 && now_us >= header.sent_ts_us) {
            const double sample_us = static_cast<double> (now_us - header.sent_ts_us);
            *lat_sum += sample_us;
            (*lat_count)++;
            if (lat_samples)
                lat_samples->add (sample_us);
        }
    }

    zlink_msg_close (&msg);
    return recv_ok;
}

inline bool drain_non_blocking_messages (
  void *server,
  size_t expected_msg_size,
  uint32_t expected_run_id,
  perf_metric::phase_t expected_phase,
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
          expected_msg_size,
          expected_run_id,
          expected_phase,
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
  size_t expected_msg_size,
  uint32_t expected_run_id,
  perf_metric::phase_t expected_phase,
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
          expected_msg_size,
          expected_run_id,
          expected_phase,
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
              expected_msg_size,
              expected_run_id,
              expected_phase,
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

inline void normalize_latency_stats (double lat_sum,
                                     long lat_count,
                                     bench_latency_sampler_t *samples,
                                     bench_latency_stats_t *latency_out)
{
    if (!latency_out)
        return;
    if (lat_count <= 0) {
        *latency_out = bench_latency_stats_t ();
        return;
    }

    bench_latency_stats_t stats = samples ? samples->snapshot () : bench_latency_stats_t ();
    if (stats.mean_us <= 0.0)
        stats.mean_us = lat_sum / static_cast<double> (lat_count);
    if (stats.p95_us <= 0.0)
        stats.p95_us = stats.mean_us;
    if (stats.p99_us <= 0.0)
        stats.p99_us = stats.p95_us;
    if (stats.p95_us < stats.mean_us)
        stats.p95_us = stats.mean_us;
    if (stats.p99_us < stats.p95_us)
        stats.p99_us = stats.p95_us;
    *latency_out = stats;
}

inline bool run_one_size_benchmark (
  void *server,
  const bench_settings_t &settings,
  size_t msg_size,
  uint32_t run_id,
  const std::string &lib_name,
  const std::string &transport)
{
    const double warmup_s =
      static_cast<double> (std::max (0, settings.warmup_seconds));
    const double settle_s =
      static_cast<double> (std::max (0, settings.settle_ms)) / 1000.0;
    const double active_s =
      static_cast<double> (std::max (1, settings.duration_seconds));

    if (!run_receive_window (
          server,
          msg_size,
          run_id,
          perf_metric::phase_warmup,
          warmup_s,
          false,
          false,
          NULL,
          NULL,
          NULL,
          NULL)) {
        return false;
    }

    if (!run_receive_window (
          server,
          msg_size,
          run_id,
          perf_metric::phase_drain,
          settle_s,
          false,
          false,
          NULL,
          NULL,
          NULL,
          NULL)) {
        return false;
    }

    long recv_count = 0;
    double lat_sum = 0.0;
    long lat_count = 0;
    bench_latency_sampler_t lat_samples;

    if (!run_receive_window (
          server,
          msg_size,
          run_id,
          perf_metric::phase_active,
          active_s,
          true,
          true,
          &recv_count,
          &lat_sum,
          &lat_count,
          &lat_samples)) {
        return false;
    }

    if (recv_count <= 0 || lat_count <= 0)
        return false;

    bench_latency_stats_t latency;
    normalize_latency_stats (lat_sum, lat_count, &lat_samples, &latency);

    const double throughput =
      static_cast<double> (recv_count)
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

    return true;
}

inline void print_server_resource_metrics (
  const std::string &lib_name,
  const std::string &transport,
  const std::vector<size_t> &sizes,
  const bench_resource_metrics_t &metrics)
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
    set_perf_pattern_env (k_pattern);

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

    const bench_settings_t settings = resolve_bench_settings ();
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

    const bench_cpu_sample_t sample_start = bench_capture_cpu_sample ();

    bool ok = true;
    for (size_t si = 0; si < sizes.size (); ++si) {
        if (g_stop_requested.load (std::memory_order_acquire)) {
            ok = false;
            break;
        }

        const uint32_t run_id = static_cast<uint32_t> (si + 1);
        if (!run_one_size_benchmark (
              server,
              settings,
              sizes[si],
              run_id,
              lib_name,
              transport)) {
            ok = false;
            break;
        }
    }

    const bench_resource_metrics_t metrics =
      bench_finish_resource_probe (sample_start);
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
