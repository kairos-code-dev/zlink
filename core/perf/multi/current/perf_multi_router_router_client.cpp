#include "../common/perf_multi_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_client_helpers.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

static const char *k_pattern = "MULTI_ROUTER_ROUTER";
static const int k_client_socket_type = ZLINK_ROUTER;
static const bool k_client_router_send = true;
static const char *k_server_routing_id = "SERVER";

using perf_multi_client::backoff_worker_idle;
using perf_multi_client::close_client_monitors;
using perf_multi_client::close_client_sockets;
using perf_multi_client::is_supported_transport;
using perf_multi_client::parse_endpoint_arg;
using perf_multi_client::recv_one_message;
using perf_multi_client::send_error;
using perf_multi_client::send_ok;
using perf_multi_client::wait_all_client_connect_ready;
using send_status_t = perf_multi_client::send_status_t;

inline send_status_t send_echo_message (void *socket,
                                        const std::string &server_id,
                                        const std::vector<char> &payload,
                                        size_t payload_size)
{
    return perf_multi_client::send_echo_message (
      socket,
      server_id,
      payload,
      payload_size,
      k_client_router_send);
}

inline bool create_client_sockets (
  ctx_guard_t &ctx,
  const std::string &transport,
  const std::string &endpoint,
  const multi_bench_settings_t &settings,
  std::vector<void *> *sockets_out,
  std::vector<connect_monitor_t> *monitors_out)
{
    return perf_multi_client::create_client_sockets (
      ctx,
      transport,
      endpoint,
      settings,
      k_client_socket_type,
      sockets_out,
      monitors_out);
}

inline bool run_echo_window_thread_pool (
  const std::vector<void *> &sockets,
  const multi_bench_settings_t &settings,
  const std::vector<char> &payload,
  size_t payload_size,
  size_t scratch_capacity,
  const std::string &server_id,
  double duration_seconds,
  bool allow_send,
  long *recv_total)
{
    if (sockets.empty ())
        return false;
    if (duration_seconds <= 0.0) {
        if (recv_total)
            *recv_total = 0;
        return true;
    }

    bool fatal_error = false;
    long local_recv = 0;
    size_t rr = 0;

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::duration_cast<std::chrono::steady_clock::duration> (
        std::chrono::duration<double> (std::max (0.0, duration_seconds)));

    const int poll_timeout_ms = std::max (0, settings.client_poll_timeout_ms);
    const size_t scratch_size =
      std::max<size_t> (scratch_capacity, static_cast<size_t> (64));

    std::vector<char> scratch (scratch_size, '\0');
    std::vector<uint8_t> awaiting_reply (sockets.size (), 0);
    std::vector<zlink_pollitem_t> poll_items (sockets.size ());
    for (size_t i = 0; i < sockets.size (); ++i) {
        const zlink_pollitem_t item = {
          sockets[i],
          0,
          ZLINK_POLLIN,
          0,
        };
        poll_items[i] = item;
    }

    while (std::chrono::steady_clock::now () < deadline && !fatal_error) {
        bool progressed = false;

        if (allow_send) {
            const size_t start_rr = rr;
            for (size_t attempts = 0; attempts < sockets.size (); ++attempts) {
                const size_t idx = (start_rr + attempts) % sockets.size ();
                if (awaiting_reply[idx] != 0)
                    continue;

                const send_status_t send_rc = send_echo_message (
                  sockets[idx],
                  server_id,
                  payload,
                  payload_size);
                if (send_rc == send_ok) {
                    awaiting_reply[idx] = 1;
                    progressed = true;
                } else if (send_rc == send_error) {
                    fatal_error = true;
                    break;
                }
            }
            rr = (start_rr + 1) % sockets.size ();
        }

        if (fatal_error)
            break;

        for (size_t i = 0; i < poll_items.size (); ++i)
            poll_items[i].revents = 0;

        const int prc = zlink_poll (
          &poll_items[0],
          static_cast<int> (poll_items.size ()),
          poll_timeout_ms);
        if (prc < 0) {
            if (zlink_errno () != EINTR) {
                fatal_error = true;
                break;
            }
        } else if (prc > 0) {
            for (size_t i = 0; i < poll_items.size (); ++i) {
                if ((poll_items[i].revents & ZLINK_POLLIN) == 0)
                    continue;

                const int recv_rc =
                  recv_one_message (sockets[i], scratch, ZLINK_DONTWAIT, 0);
                if (recv_rc < 0) {
                    fatal_error = true;
                    break;
                }
                if (recv_rc <= 0)
                    continue;

                progressed = true;
                ++local_recv;
                awaiting_reply[i] = 0;

                if (!allow_send)
                    continue;

                const send_status_t send_rc = send_echo_message (
                  sockets[i],
                  server_id,
                  payload,
                  payload_size);
                if (send_rc == send_ok) {
                    awaiting_reply[i] = 1;
                    progressed = true;
                } else if (send_rc == send_error) {
                    fatal_error = true;
                    break;
                }
            }
        }

        if (fatal_error)
            break;

        if (!progressed)
            backoff_worker_idle (settings);
    }

    if (recv_total)
        *recv_total = local_recv;

    return !fatal_error;
}

inline bench_latency_stats_t measure_echo_latency_stats_us (
  const std::vector<void *> &sockets,
  const std::string &server_id,
  const std::vector<char> &payload,
  size_t payload_size,
  std::vector<char> &scratch)
{
    bench_latency_stats_t empty;
    if (sockets.empty ())
        return empty;

    void *send_socket = sockets[0];
    if (!send_socket)
        return empty;

    zlink_pollitem_t item = {send_socket, 0, ZLINK_POLLIN, 0};

    const int lat_count = std::max (1, resolve_bench_count ("PERF_LAT_COUNT", 200));
    const int lat_timeout_ms =
      std::max (1, resolve_bench_count ("PERF_MULTI_LAT_TIMEOUT_MS", 5000));
    bench_latency_sampler_t lat_samples;

    for (int i = 0; i < lat_count; ++i) {
        stopwatch_t per_roundtrip;
        per_roundtrip.start ();
        const send_status_t send_rc = send_echo_message (
          send_socket,
          server_id,
          payload,
          payload_size);
        if (send_rc != send_ok)
            break;

        bool got_reply = false;
        const auto deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::milliseconds (lat_timeout_ms);
        while (std::chrono::steady_clock::now () < deadline) {
            item.revents = 0;

            const int prc = zlink_poll (&item, 1, 1);
            if (prc < 0) {
                if (zlink_errno () == EINTR)
                    continue;
                return empty;
            }
            if (prc == 0)
                continue;
            if ((item.revents & ZLINK_POLLIN) == 0)
                continue;

            const int rc =
              recv_one_message (send_socket, scratch, ZLINK_DONTWAIT, 0);
            if (rc < 0)
                return empty;
            if (rc > 0) {
                got_reply = true;
                break;
            }
        }

        if (!got_reply)
            break;
        lat_samples.add ((per_roundtrip.elapsed_ms () * 1000.0) / 2.0);
    }

    return lat_samples.snapshot ();
}

inline bool run_echo_duration (
  const std::vector<void *> &sockets,
  const multi_bench_settings_t &settings,
  const std::vector<char> &payload,
  size_t payload_size,
  size_t scratch_capacity,
  const std::string &server_id,
  std::vector<char> &lat_scratch,
  double *throughput_out,
  bench_latency_stats_t *latency_out,
  bench_multi_resource_metrics_t *metrics_out)
{
    if (!throughput_out || !latency_out || !metrics_out)
        return false;

    *throughput_out = 0.0;
    *latency_out = bench_latency_stats_t ();

    if (sockets.empty ())
        return false;

    if (!run_echo_window_thread_pool (
          sockets,
          settings,
          payload,
          payload_size,
          scratch_capacity,
          server_id,
          static_cast<double> (std::max (0, settings.warmup_seconds)),
          true,
          NULL)) {
        return false;
    }

    if (settings.settle_ms > 0) {
        std::this_thread::sleep_for (
          std::chrono::milliseconds (settings.settle_ms));
    }

    long recv_count = 0;
    const bench_multi_cpu_sample_t sample_start = bench_multi_capture_cpu_sample ();
    if (!run_echo_window_thread_pool (
          sockets,
          settings,
          payload,
          payload_size,
          scratch_capacity,
          server_id,
          static_cast<double> (std::max (1, settings.duration_seconds)),
          true,
          &recv_count)) {
        return false;
    }
    *metrics_out = bench_multi_finish_resource_probe (sample_start);

    const double drain_seconds =
      static_cast<double> (std::max (0, settings.drain_ms)) / 1000.0;
    if (drain_seconds > 0.0) {
        if (!run_echo_window_thread_pool (
              sockets,
              settings,
              payload,
              payload_size,
              scratch_capacity,
              server_id,
              drain_seconds,
              false,
              NULL)) {
            return false;
        }
    }

    *throughput_out = static_cast<double> (recv_count)
                      / static_cast<double> (std::max (1, settings.duration_seconds));
    if (recv_count <= 0)
        return false;

    *latency_out = measure_echo_latency_stats_us (
      sockets,
      server_id,
      payload,
      payload_size,
      lat_scratch);
    bool estimated_from_throughput = false;
    if (latency_out->mean_us <= 0.0 && *throughput_out > 0.0) {
        latency_out->mean_us = 1000000.0 / *throughput_out;
        estimated_from_throughput = true;
    }
    if (latency_out->p95_us <= 0.0) {
        latency_out->p95_us = estimated_from_throughput
                                ? latency_out->mean_us * 1.25
                                : latency_out->mean_us;
    }
    if (latency_out->p99_us <= 0.0) {
        latency_out->p99_us = estimated_from_throughput
                                ? latency_out->mean_us * 1.50
                                : latency_out->p95_us;
    }
    if (latency_out->p95_us < latency_out->mean_us)
        latency_out->p95_us = latency_out->mean_us;
    if (latency_out->p99_us < latency_out->p95_us)
        latency_out->p99_us = latency_out->p95_us;

    return true;
}

inline void print_client_result_lines (
  const std::string &lib_name,
  const std::string &transport,
  size_t msg_size,
  double throughput,
  const bench_latency_stats_t &latency,
  const bench_multi_resource_metrics_t &metrics)
{
    print_result (
      lib_name,
      k_pattern,
      transport,
      msg_size,
      throughput,
      latency.mean_us,
      latency.p95_us,
      latency.p99_us);

    if (metrics.has_cpu_pct) {
        std::cout << "RESULT," << lib_name << "," << k_pattern << ","
                  << transport << "," << msg_size << ",client_cpu_pct,"
                  << std::fixed << std::setprecision (2) << metrics.cpu_pct
                  << std::endl;
    }

    if (metrics.has_mem_mb) {
        std::cout << "RESULT," << lib_name << "," << k_pattern << ","
                  << transport << "," << msg_size << ",client_mem_mb,"
                  << std::fixed << std::setprecision (2) << metrics.mem_mb
                  << std::endl;
    }
}

inline std::vector<size_t> resolve_case_msg_sizes (size_t fallback_size)
{
    std::vector<size_t> msg_sizes = resolve_bench_msg_sizes (fallback_size);
    if (msg_sizes.empty ())
        msg_sizes.push_back (fallback_size > 0 ? fallback_size : 64);
    return msg_sizes;
}

inline size_t resolve_case_max_msg_size (size_t fallback_size,
                                         const std::vector<size_t> &msg_sizes)
{
    size_t max_msg_size = fallback_size > 0 ? fallback_size : 64;
    for (size_t i = 0; i < msg_sizes.size (); ++i) {
        if (msg_sizes[i] > max_msg_size)
            max_msg_size = msg_sizes[i];
    }
    return max_msg_size;
}

inline bool run_single_size_case (const std::vector<void *> &sockets,
                                  const multi_bench_settings_t &base_settings,
                                  const std::vector<char> &payload,
                                  size_t scratch_capacity,
                                  const std::string &server_id,
                                  std::vector<char> *latency_scratch,
                                  const std::string &lib_name,
                                  const std::string &transport,
                                  size_t msg_size)
{
    if (!latency_scratch)
        return false;

    multi_bench_settings_t settings = base_settings;
    const size_t payload_size = std::max<size_t> (msg_size, 64);

    double throughput = 0.0;
    bench_latency_stats_t latency;
    bench_multi_resource_metrics_t metrics;
    if (!run_echo_duration (
          sockets,
          settings,
          payload,
          payload_size,
          scratch_capacity,
          server_id,
          *latency_scratch,
          &throughput,
          &latency,
          &metrics)) {
        return false;
    }

    print_client_result_lines (
      lib_name,
      transport,
      msg_size,
      throughput,
      latency,
      metrics);

    return true;
}

inline int run_client_benchmark (const std::string &lib_name,
                                 const std::string &transport,
                                 const std::string &endpoint,
                                 size_t fallback_size)
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

    const multi_bench_settings_t base_settings = resolve_multi_bench_settings ();
    const std::vector<size_t> msg_sizes = resolve_case_msg_sizes (fallback_size);
    const size_t max_msg_size =
      resolve_case_max_msg_size (fallback_size, msg_sizes);

    ctx_guard_t ctx;
    if (!ctx.valid ())
        return 1;

    std::vector<void *> sockets;
    std::vector<connect_monitor_t> monitors;
    if (!create_client_sockets (
          ctx,
          transport,
          endpoint,
          base_settings,
          &sockets,
          &monitors)) {
        close_client_monitors (&monitors);
        close_client_sockets (&sockets);
        return 1;
    }

    if (!wait_all_client_connect_ready (
          monitors,
          base_settings.connect_ready_timeout_ms)) {
        close_client_monitors (&monitors);
        close_client_sockets (&sockets);
        return 1;
    }
    close_client_monitors (&monitors);

    const std::string server_id = k_server_routing_id;
    const size_t payload_capacity = std::max<size_t> (max_msg_size, 64);
    const size_t scratch_capacity = static_cast<size_t> (64);

    std::vector<char> payload (payload_capacity, 'c');
    std::vector<char> latency_scratch (scratch_capacity, '\0');

    for (size_t si = 0; si < msg_sizes.size (); ++si) {
        const size_t msg_size = msg_sizes[si];
        if (!run_single_size_case (
              sockets,
              base_settings,
              payload,
              scratch_capacity,
              server_id,
              &latency_scratch,
              lib_name,
              transport,
              msg_size)) {
            close_client_sockets (&sockets);
            return 1;
        }

        run_size_transition_drain_stage (
          base_settings, (si + 1) < msg_sizes.size ());
    }
    close_client_sockets (&sockets);
    return 0;
}

} // namespace

int main (int argc, char **argv)
{
    if (argc < 4)
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t fallback_size =
      static_cast<size_t> (std::strtoull (argv[3], NULL, 10));

    std::string endpoint;
    if (!parse_endpoint_arg (argc, argv, &endpoint)) {
        std::cerr << "missing --endpoint" << std::endl;
        return 1;
    }

    return run_client_benchmark (lib_name, transport, endpoint, fallback_size);
}
