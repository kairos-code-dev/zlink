#include "../common/perf_multi_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
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

enum send_status_t
{
    send_ok = 0,
    send_would_block = 1,
    send_error = 2
};

struct pubsub_worker_stats_t
{
    long recv_count;
    double lat_sum;
    long lat_count;
    bool failed;

    pubsub_worker_stats_t ()
        : recv_count (0),
          lat_sum (0.0),
          lat_count (0),
          failed (false)
    {
    }
};

inline bool is_supported_transport (const std::string &transport)
{
    return transport == "tcp" || transport == "tls" || transport == "ws"
           || transport == "wss";
}

inline bool parse_endpoint_arg (int argc,
                                char **argv,
                                std::string *endpoint_out)
{
    if (!endpoint_out)
        return false;

    endpoint_out->clear ();
    for (int i = 4; i + 1 < argc; ++i) {
        if (std::strcmp (argv[i], "--endpoint") == 0) {
            *endpoint_out = argv[i + 1];
            return !endpoint_out->empty ();
        }
    }

    return false;
}

inline send_status_t classify_send_result (int rc)
{
    if (rc >= 0)
        return send_ok;
    const int err = zlink_errno ();
    if (err == EAGAIN || err == EINTR)
        return send_would_block;
    return send_error;
}

inline send_status_t send_echo_message (void *socket,
                                        const std::string &server_id,
                                        const std::vector<char> &payload,
                                        size_t payload_size,
                                        bool non_blocking)
{
    const int base_flags = non_blocking ? ZLINK_DONTWAIT : 0;

    if (k_client_router_send) {
        const int id_rc = zlink_send (
          socket,
          server_id.c_str (),
          server_id.size (),
          ZLINK_SNDMORE | base_flags);
        const send_status_t id_status = classify_send_result (id_rc);
        if (id_status != send_ok)
            return id_status;
    }

    const int payload_rc = zlink_send (
      socket,
      payload.data (),
      payload_size,
      base_flags);
    return classify_send_result (payload_rc);
}

inline int recv_one_message (void *socket,
                             std::vector<char> &scratch,
                             int flags)
{
    const int rc = zlink_recv (socket, scratch.data (), scratch.size (), flags);
    if (rc < 0) {
        const int err = zlink_errno ();
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }

    while (true) {
        int more = 0;
        size_t more_size = sizeof (more);
        if (zlink_getsockopt (socket, ZLINK_RCVMORE, &more, &more_size) != 0)
            break;
        if (!more)
            break;

        const int next_rc = zlink_recv (socket, scratch.data (), scratch.size (), 0);
        if (next_rc < 0) {
            if (zlink_errno () == EINTR)
                continue;
            return -1;
        }
    }

    return 1;
}

inline bool wait_all_client_connect_ready (
  std::vector<connect_monitor_t> &monitors,
  int timeout_ms)
{
    if (monitors.empty ())
        return true;

    std::vector<zlink_pollitem_t> items (monitors.size ());
    std::vector<char> ready (monitors.size (), 0);
    for (size_t i = 0; i < monitors.size (); ++i) {
        if (!monitors[i].monitor)
            return false;
        const zlink_pollitem_t item = {monitors[i].monitor, 0, ZLINK_POLLIN, 0};
        items[i] = item;
    }

    size_t ready_count = 0;
    const int bounded_timeout = timeout_ms > 0 ? timeout_ms : 0;
    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (bounded_timeout);

    while (ready_count < monitors.size ()) {
        const auto now = std::chrono::steady_clock::now ();
        if (now >= deadline)
            return false;

        for (size_t i = 0; i < items.size (); ++i)
            items[i].revents = 0;

        const long remain_ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                                 deadline - now)
                                 .count ();
        const int prc = zlink_poll (
          &items[0],
          static_cast<int> (items.size ()),
          remain_ms > 0 ? remain_ms : 0);
        if (prc < 0) {
            if (zlink_errno () == EINTR)
                continue;
            return false;
        }
        if (prc == 0)
            continue;

        for (size_t i = 0; i < items.size (); ++i) {
            if (ready[i])
                continue;
            if ((items[i].revents & ZLINK_POLLIN) == 0)
                continue;
            if (poll_connect_ready_count (monitors[i]) <= 0)
                continue;
            ready[i] = 1;
            ++ready_count;
        }
    }

    return true;
}

inline void close_client_sockets (std::vector<void *> *sockets)
{
    if (!sockets)
        return;

    for (size_t i = 0; i < sockets->size (); ++i) {
        if ((*sockets)[i]) {
            zlink_close ((*sockets)[i]);
            (*sockets)[i] = NULL;
        }
    }
}

inline void close_client_monitors (std::vector<connect_monitor_t> *monitors)
{
    if (!monitors)
        return;

    for (size_t i = 0; i < monitors->size (); ++i)
        close_connect_monitor ((*monitors)[i]);
}

inline bool create_client_sockets (
  ctx_guard_t &ctx,
  const std::string &transport,
  const std::string &endpoint,
  const multi_bench_settings_t &settings,
  std::vector<void *> *sockets_out,
  std::vector<connect_monitor_t> *monitors_out)
{
    if (!sockets_out || !monitors_out)
        return false;

    sockets_out->assign (settings.clients, NULL);
    monitors_out->assign (settings.clients, connect_monitor_t ());

    const int linger_ms = 0;
    for (size_t i = 0; i < sockets_out->size (); ++i) {
        void *sock = zlink_socket (ctx.get (), k_client_socket_type);
        if (!sock)
            return false;

        set_sockopt_int (sock, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
        apply_benchmark_hwm (sock, settings.hwm);

        if (k_client_socket_type == ZLINK_ROUTER) {
            char id_buf[32];
            const int id_len =
              std::snprintf (id_buf, sizeof (id_buf), "client_%zu", i);
            if (id_len > 0) {
                zlink_setsockopt (
                  sock,
                  ZLINK_ROUTING_ID,
                  id_buf,
                  static_cast<size_t> (id_len));
            }
        }

        if (k_client_socket_type == ZLINK_SUB)
            zlink_setsockopt (sock, ZLINK_SUBSCRIBE, "", 0);

        if (!setup_tls_client (sock, transport)) {
            zlink_close (sock);
            return false;
        }

        if (!open_connect_monitor (sock, (*monitors_out)[i])) {
            zlink_close (sock);
            return false;
        }

        if (zlink_connect (sock, endpoint.c_str ()) != 0) {
            std::cerr << "connect failed for " << endpoint << ": "
                      << zlink_strerror (zlink_errno ()) << std::endl;
            close_connect_monitor ((*monitors_out)[i]);
            zlink_close (sock);
            return false;
        }
        apply_debug_timeouts (sock, transport);

        (*sockets_out)[i] = sock;
    }

    return true;
}

inline size_t resolve_worker_count (const multi_bench_settings_t &settings,
                                    size_t socket_count)
{
    if (socket_count == 0)
        return 0;

    const size_t configured = static_cast<size_t> (
      std::max (1, settings.client_workers));
    return std::max<size_t> (1, std::min<size_t> (socket_count, configured));
}

inline void build_worker_assignments (size_t socket_count,
                                      size_t worker_count,
                                      std::vector<std::vector<size_t> > *out)
{
    if (!out)
        return;

    out->assign (worker_count, std::vector<size_t> ());
    for (size_t i = 0; i < socket_count; ++i)
        (*out)[i % worker_count].push_back (i);
}

inline void backoff_worker_idle (const multi_bench_settings_t &settings)
{
    if (settings.client_idle_sleep_us > 0) {
        std::this_thread::sleep_for (
          std::chrono::microseconds (settings.client_idle_sleep_us));
        return;
    }
    std::this_thread::yield ();
}

inline bool drain_socket_non_blocking (void *socket,
                                       std::vector<char> &scratch,
                                       long *recv_count,
                                       double *lat_sum,
                                       long *lat_count)
{
    if (!socket)
        return false;

    long local_recv = 0;
    while (true) {
        const int rc = recv_one_message (socket, scratch, ZLINK_DONTWAIT);
        if (rc < 0)
            return false;
        if (rc == 0)
            break;

        ++local_recv;
        if (lat_sum && lat_count
            && scratch.size () >= sizeof (unsigned long long)) {
            unsigned long long sent_us = 0;
            std::memcpy (&sent_us, scratch.data (), sizeof (sent_us));
            const unsigned long long now_us = static_cast<unsigned long long> (
              std::chrono::duration_cast<std::chrono::microseconds> (
                std::chrono::system_clock::now ().time_since_epoch ())
                .count ());
            if (now_us >= sent_us) {
                *lat_sum += static_cast<double> (now_us - sent_us);
                (*lat_count)++;
            }
        }
    }

    if (recv_count)
        *recv_count += local_recv;
    return true;
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
            const size_t send_attempts = std::max<size_t> (1, owned.size ());
            for (size_t a = 0; a < send_attempts; ++a) {
                const size_t idx = owned[rr % owned.size ()];
                ++rr;
                const send_status_t send_rc = send_echo_message (
                  sockets[idx],
                  server_id,
                  payload,
                  payload_size,
                  true);
                if (send_rc == send_ok) {
                    progressed = true;
                    continue;
                }
                if (send_rc == send_would_block)
                    continue;
                fatal_error->store (true, std::memory_order_release);
                break;
            }
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
                      collect_latency ? &stats->lat_count : NULL)) {
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
  long *lat_count)
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
        return true;
    }

    const size_t worker_count = resolve_worker_count (settings, sockets.size ());
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

    return !failed;
}

inline double measure_echo_latency_us (
  const std::vector<void *> &sockets,
  const std::string &server_id,
  const std::vector<char> &payload,
  size_t payload_size,
  std::vector<char> &scratch)
{
    if (sockets.empty ())
        return 0.0;

    void *send_socket = sockets[0];
    if (!send_socket)
        return 0.0;

    zlink_pollitem_t item = {send_socket, 0, ZLINK_POLLIN, 0};

    const int lat_count = std::max (1, resolve_bench_count ("PERF_LAT_COUNT", 200));
    const int lat_timeout_ms =
      std::max (1, resolve_bench_count ("PERF_MULTI_LAT_TIMEOUT_MS", 5000));
    int completed = 0;

    stopwatch_t sw;
    sw.start ();

    for (int i = 0; i < lat_count; ++i) {
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
                return 0.0;
            }
            if (prc == 0)
                continue;
            if ((item.revents & ZLINK_POLLIN) == 0)
                continue;

            const int rc = recv_one_message (send_socket, scratch, ZLINK_DONTWAIT);
            if (rc < 0)
                return 0.0;
            if (rc > 0) {
                got_reply = true;
                break;
            }
        }

        if (!got_reply)
            break;
        ++completed;
    }

    if (completed <= 0)
        return 0.0;

    const double divisor =
      k_one_way_latency ? static_cast<double> (completed)
                        : static_cast<double> (completed) * 2.0;
    return (sw.elapsed_ms () * 1000.0) / divisor;
}

inline bool run_pubsub_duration (
  const std::vector<void *> &subs,
  const multi_bench_settings_t &settings,
  size_t scratch_capacity,
  double *throughput_out,
  double *latency_out,
  bench_multi_resource_metrics_t *metrics_out)
{
    if (!throughput_out || !latency_out || !metrics_out)
        return false;

    *throughput_out = 0.0;
    *latency_out = 0.0;

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
          NULL)) {
        return false;
    }

    if (settings.settle_ms > 0) {
        std::this_thread::sleep_for (
          std::chrono::milliseconds (settings.settle_ms));
    }

    long recv_count = 0;
    double lat_sum = 0.0;
    long lat_count = 0;

    const bench_multi_cpu_sample_t sample_start = bench_multi_capture_cpu_sample ();
    if (!run_pubsub_window_thread_pool (
          subs,
          settings,
          scratch_capacity,
          static_cast<double> (std::max (1, settings.duration_seconds)),
          true,
          &recv_count,
          &lat_sum,
          &lat_count)) {
        return false;
    }
    *metrics_out = bench_multi_finish_resource_probe (sample_start);

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
              NULL)) {
            return false;
        }
    }

    *throughput_out = static_cast<double> (recv_count)
                      / static_cast<double> (std::max (1, settings.duration_seconds));
    if (recv_count <= 0 || lat_count <= 0)
        return false;

    *latency_out = lat_sum / static_cast<double> (lat_count);
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
  double *latency_out,
  bench_multi_resource_metrics_t *metrics_out)
{
    if (!throughput_out || !latency_out || !metrics_out)
        return false;

    *throughput_out = 0.0;
    *latency_out = 0.0;

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

    *latency_out = measure_echo_latency_us (
      sockets,
      server_id,
      payload,
      payload_size,
      lat_scratch);
    if (*latency_out <= 0.0)
        *latency_out = 0.0;

    return true;
}

inline void print_client_result_lines (
  const std::string &lib_name,
  const std::string &transport,
  size_t msg_size,
  double throughput,
  double latency,
  const bench_multi_resource_metrics_t &metrics)
{
    print_result (
      lib_name,
      k_pattern,
      transport,
      msg_size,
      throughput,
      latency);

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
    double latency = 0.0;
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
