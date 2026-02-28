#include "../common/perf_multi_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_client_helpers.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

static const char *k_pattern = "MULTI_DEALER_ROUTER";
static const int k_client_socket_type = ZLINK_DEALER;
static const bool k_client_router_send = false;
static const bool k_pubsub_mode = false;
static const bool k_one_way_latency = false;
static const char *k_server_routing_id = "SERVER";

using perf_multi_client::backoff_worker_idle;
using perf_multi_client::build_worker_assignments;
using perf_multi_client::close_client_monitors;
using perf_multi_client::close_client_sockets;
using perf_multi_client::drain_socket_non_blocking;
using perf_multi_client::is_supported_transport;
using perf_multi_client::parse_endpoint_arg;
using perf_multi_client::recv_one_message;
using perf_multi_client::resolve_worker_count;
using perf_multi_client::send_error;
using perf_multi_client::send_ok;
using perf_multi_client::wait_all_client_connect_ready;
using send_status_t = perf_multi_client::send_status_t;
using pubsub_worker_stats_t = perf_multi_client::pubsub_worker_stats_t;

inline send_status_t send_echo_message (void *socket,
                                        const std::string &server_id,
                                        const std::vector<char> &payload,
                                        size_t payload_size,
                                        bool non_blocking)
{
    return perf_multi_client::send_echo_message (
      socket,
      server_id,
      payload,
      payload_size,
      non_blocking,
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

inline void run_echo_worker_loop (
  const std::vector<void *> &sockets,
  const std::vector<size_t> &owned,
  const multi_bench_settings_t &settings,
  const std::vector<char> &payload,
  size_t payload_size,
  const std::string &server_id,
  size_t scratch_size,
  const std::chrono::steady_clock::time_point &deadline,
  int poll_timeout_ms,
  bool allow_send,
  std::atomic<bool> *fatal_error,
  long *local_recv_out)
{
    if (!fatal_error || !local_recv_out)
        return;

    long local_recv = 0;
    size_t rr = 0;
    std::vector<char> scratch (scratch_size, '\0');
    std::vector<zlink_pollitem_t> poll_items (owned.size ());
    for (size_t i = 0; i < owned.size (); ++i) {
        const zlink_pollitem_t item = {
          sockets[owned[i]],
          0,
          ZLINK_POLLIN,
          0,
        };
        poll_items[i] = item;
    }

    while (std::chrono::steady_clock::now () < deadline
           && !fatal_error->load (std::memory_order_acquire)) {
        bool progressed = false;

        if (allow_send) {
            const size_t idx = owned[rr % owned.size ()];
            ++rr;
            const send_status_t send_rc = send_echo_message (
              sockets[idx],
              server_id,
              payload,
              payload_size,
              true);
            if (send_rc == send_ok)
                progressed = true;
            else if (send_rc == send_error)
                fatal_error->store (true, std::memory_order_release);
        }

        if (fatal_error->load (std::memory_order_acquire))
            break;

        for (size_t i = 0; i < poll_items.size (); ++i)
            poll_items[i].revents = 0;

        const int prc = zlink_poll (
          &poll_items[0],
          static_cast<int> (poll_items.size ()),
          poll_timeout_ms);
        if (prc < 0) {
            if (zlink_errno () != EINTR) {
                fatal_error->store (true, std::memory_order_release);
                break;
            }
        } else if (prc > 0) {
            for (size_t i = 0; i < poll_items.size (); ++i) {
                if ((poll_items[i].revents & ZLINK_POLLIN) == 0)
                    continue;

                long recv_now = 0;
                if (!drain_socket_non_blocking (
                      sockets[owned[i]],
                      scratch,
                      &recv_now,
                      NULL,
                      NULL,
                      NULL)) {
                    fatal_error->store (true, std::memory_order_release);
                    break;
                }
                if (recv_now > 0) {
                    progressed = true;
                    local_recv += recv_now;
                }
            }
        }

        if (fatal_error->load (std::memory_order_acquire))
            break;

        if (!progressed)
            backoff_worker_idle (settings);
    }

    *local_recv_out = local_recv;
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

    const size_t worker_count = resolve_worker_count (settings, sockets.size ());
    std::vector<std::vector<size_t> > worker_assign;
    build_worker_assignments (sockets.size (), worker_count, &worker_assign);

    std::atomic<bool> fatal_error (false);
    std::atomic<long> recv_accum (0);

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::duration_cast<std::chrono::steady_clock::duration> (
        std::chrono::duration<double> (std::max (0.0, duration_seconds)));

    const int poll_timeout_ms = std::max (0, settings.client_poll_timeout_ms);
    const size_t scratch_size =
      std::max<size_t> (scratch_capacity, static_cast<size_t> (1024));

    std::vector<std::thread> workers;
    workers.reserve (worker_count);
    for (size_t w = 0; w < worker_count; ++w) {
        workers.push_back (std::thread ([&, w] () {
            long local_recv = 0;
            run_echo_worker_loop (
              sockets,
              worker_assign[w],
              settings,
              payload,
              payload_size,
              server_id,
              scratch_size,
              deadline,
              poll_timeout_ms,
              allow_send,
              &fatal_error,
              &local_recv);

            recv_accum.fetch_add (local_recv, std::memory_order_relaxed);
        }));
    }

    for (size_t i = 0; i < workers.size (); ++i) {
        if (workers[i].joinable ())
            workers[i].join ();
    }

    if (recv_total)
        *recv_total = recv_accum.load (std::memory_order_relaxed);

    return !fatal_error.load (std::memory_order_acquire);
}

inline void run_pubsub_worker_loop (
  const std::vector<void *> &sockets,
  const std::vector<size_t> &owned,
  const multi_bench_settings_t &settings,
  size_t scratch_size,
  const std::chrono::steady_clock::time_point &deadline,
  int poll_timeout_ms,
  bool collect_latency,
  std::atomic<bool> *fatal_error,
  pubsub_worker_stats_t *stats)
{
    if (!fatal_error || !stats)
        return;

    std::vector<char> scratch (scratch_size, '\0');
    std::vector<zlink_pollitem_t> poll_items (owned.size ());
    for (size_t i = 0; i < owned.size (); ++i) {
        const zlink_pollitem_t item = {
          sockets[owned[i]],
          0,
          ZLINK_POLLIN,
          0,
        };
        poll_items[i] = item;
    }

    while (std::chrono::steady_clock::now () < deadline
           && !fatal_error->load (std::memory_order_acquire)) {
        bool progressed = false;

        for (size_t i = 0; i < poll_items.size (); ++i)
            poll_items[i].revents = 0;

        const int prc = zlink_poll (
          &poll_items[0],
          static_cast<int> (poll_items.size ()),
          poll_timeout_ms);
        if (prc < 0) {
            if (zlink_errno () != EINTR) {
                fatal_error->store (true, std::memory_order_release);
                stats->failed = true;
                break;
            }
        } else if (prc > 0) {
            for (size_t i = 0; i < poll_items.size (); ++i) {
                if ((poll_items[i].revents & ZLINK_POLLIN) == 0)
                    continue;

                long recv_now = 0;
                if (!drain_socket_non_blocking (
                      sockets[owned[i]],
                      scratch,
                      &recv_now,
                      collect_latency ? &stats->lat_sum : NULL,
                      collect_latency ? &stats->lat_count : NULL,
                      collect_latency ? &stats->lat_samples : NULL)) {
                    fatal_error->store (true, std::memory_order_release);
                    stats->failed = true;
                    break;
                }
                if (recv_now > 0) {
                    progressed = true;
                    stats->recv_count += recv_now;
                }
            }
        }

        if (fatal_error->load (std::memory_order_acquire))
            break;

        if (!progressed)
            backoff_worker_idle (settings);
    }
}

inline bool run_pubsub_window_thread_pool (
  const std::vector<void *> &sockets,
  const multi_bench_settings_t &settings,
  size_t scratch_capacity,
  double duration_seconds,
  bool collect_latency,
  long *recv_total,
  double *lat_sum,
  long *lat_count,
  bench_latency_stats_t *latency_stats)
{
    if (sockets.empty ())
        return false;
    if (duration_seconds <= 0.0) {
        if (recv_total)
            *recv_total = 0;
        if (lat_sum)
            *lat_sum = 0.0;
        if (lat_count)
            *lat_count = 0;
        if (latency_stats)
            *latency_stats = bench_latency_stats_t ();
        return true;
    }

    const size_t worker_count =
      collect_latency ? 1 : resolve_worker_count (settings, sockets.size ());
    std::vector<std::vector<size_t> > worker_assign;
    build_worker_assignments (sockets.size (), worker_count, &worker_assign);

    std::atomic<bool> fatal_error (false);
    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::duration_cast<std::chrono::steady_clock::duration> (
        std::chrono::duration<double> (std::max (0.0, duration_seconds)));

    const int poll_timeout_ms = std::max (0, settings.client_poll_timeout_ms);
    const size_t scratch_size =
      std::max<size_t> (scratch_capacity, static_cast<size_t> (1024));

    std::vector<pubsub_worker_stats_t> worker_stats (worker_count);
    std::vector<std::thread> workers;
    workers.reserve (worker_count);

    for (size_t w = 0; w < worker_count; ++w) {
        workers.push_back (std::thread ([&, w] () {
            run_pubsub_worker_loop (
              sockets,
              worker_assign[w],
              settings,
              scratch_size,
              deadline,
              poll_timeout_ms,
              collect_latency,
              &fatal_error,
              &worker_stats[w]);
        }));
    }

    for (size_t i = 0; i < workers.size (); ++i) {
        if (workers[i].joinable ())
            workers[i].join ();
    }

    long recv_sum = 0;
    double lat_sum_local = 0.0;
    long lat_count_local = 0;
    bool failed = fatal_error.load (std::memory_order_acquire);
    for (size_t i = 0; i < worker_stats.size (); ++i) {
        recv_sum += worker_stats[i].recv_count;
        lat_sum_local += worker_stats[i].lat_sum;
        lat_count_local += worker_stats[i].lat_count;
        failed = failed || worker_stats[i].failed;
    }

    if (recv_total)
        *recv_total = recv_sum;
    if (lat_sum)
        *lat_sum = lat_sum_local;
    if (lat_count)
        *lat_count = lat_count_local;
    if (latency_stats) {
        if (collect_latency && !worker_stats.empty ())
            *latency_stats = worker_stats[0].lat_samples.snapshot ();
        else
            *latency_stats = bench_latency_stats_t ();
    }

    return !failed;
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
          payload_size,
          false);
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

            const int rc = recv_one_message (send_socket, scratch, ZLINK_DONTWAIT);
            if (rc < 0)
                return empty;
            if (rc > 0) {
                got_reply = true;
                break;
            }
        }

        if (!got_reply)
            break;
        const double divisor = k_one_way_latency ? 1.0 : 2.0;
        lat_samples.add ((per_roundtrip.elapsed_ms () * 1000.0) / divisor);
    }

    return lat_samples.snapshot ();
}

inline bool run_pubsub_duration (
  const std::vector<void *> &subs,
  const multi_bench_settings_t &settings,
  size_t scratch_capacity,
  double *throughput_out,
  bench_latency_stats_t *latency_out,
  bench_multi_resource_metrics_t *metrics_out)
{
    if (!throughput_out || !latency_out || !metrics_out)
        return false;

    *throughput_out = 0.0;
    *latency_out = bench_latency_stats_t ();

    if (subs.empty ())
        return false;

    if (!run_pubsub_window_thread_pool (
          subs,
          settings,
          scratch_capacity,
          static_cast<double> (std::max (0, settings.warmup_seconds)),
          false,
          NULL,
          NULL,
          NULL,
          NULL)) {
        return false;
    }

    if (settings.settle_ms > 0) {
        std::this_thread::sleep_for (
          std::chrono::milliseconds (settings.settle_ms));
    }

    long recv_count = 0;
    const bench_multi_cpu_sample_t sample_start = bench_multi_capture_cpu_sample ();
    if (!run_pubsub_window_thread_pool (
          subs,
          settings,
          scratch_capacity,
          static_cast<double> (std::max (1, settings.duration_seconds)),
          false,
          &recv_count,
          NULL,
          NULL,
          NULL)) {
        return false;
    }
    *metrics_out = bench_multi_finish_resource_probe (sample_start);

    double lat_sum = 0.0;
    long lat_count = 0;
    bench_latency_stats_t latency_stats;
    if (!run_pubsub_window_thread_pool (
          subs,
          settings,
          scratch_capacity,
          static_cast<double> (std::max (1, settings.duration_seconds)),
          true,
          NULL,
          &lat_sum,
          &lat_count,
          &latency_stats)) {
        return false;
    }

    const double drain_seconds =
      static_cast<double> (std::max (0, settings.drain_ms)) / 1000.0;
    if (drain_seconds > 0.0) {
        if (!run_pubsub_window_thread_pool (
              subs,
              settings,
              scratch_capacity,
              drain_seconds,
              false,
              NULL,
              NULL,
              NULL,
              NULL)) {
            return false;
        }
    }

    *throughput_out = static_cast<double> (recv_count)
                      / static_cast<double> (std::max (1, settings.duration_seconds));
    if (recv_count <= 0 || lat_count <= 0)
        return false;

    if (latency_stats.mean_us <= 0.0 && lat_count > 0)
        latency_stats.mean_us = lat_sum / static_cast<double> (lat_count);
    if (latency_stats.p95_us <= 0.0)
        latency_stats.p95_us = latency_stats.mean_us;
    if (latency_stats.p99_us <= 0.0)
        latency_stats.p99_us = latency_stats.p95_us;
    if (latency_stats.p95_us < latency_stats.mean_us)
        latency_stats.p95_us = latency_stats.mean_us;
    if (latency_stats.p99_us < latency_stats.p95_us)
        latency_stats.p99_us = latency_stats.p95_us;
    *latency_out = latency_stats;
    return true;
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
    bool ok = false;

    if (k_pubsub_mode) {
        ok = run_pubsub_duration (
          sockets,
          settings,
          scratch_capacity,
          &throughput,
          &latency,
          &metrics);
    } else {
        ok = run_echo_duration (
          sockets,
          settings,
          payload,
          payload_size,
          scratch_capacity,
          server_id,
          *latency_scratch,
          &throughput,
          &latency,
          &metrics);
    }

    if (!ok)
        return false;

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
    const size_t scratch_capacity = std::max<size_t> (
      payload_capacity + 256,
      static_cast<size_t> (1024));

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
