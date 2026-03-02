#ifndef PERF_MULTI_CLIENT_HELPERS_HPP
#define PERF_MULTI_CLIENT_HELPERS_HPP

#include "perf_common.hpp"
#include "perf_common_multi.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace perf_multi_client {

enum send_status_t
{
    send_ok = 0,
    send_blocked = 1,
    send_error = 2
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
    if (err == EAGAIN || err == EINTR || err == ENOENT
        || err == ENOTCONN || err == EHOSTUNREACH)
        return send_blocked;
    return send_error;
}

inline send_status_t send_echo_message (void *socket,
                                        const std::string &server_id,
                                        const std::vector<char> &payload,
                                        size_t payload_size,
                                        bool router_send)
{
    const int base_flags = 0;

    if (router_send) {
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
                             int flags,
                             size_t capture_bytes)
{
    if (!socket)
        return -1;

    zlink_msg_t frame;
    if (zlink_msg_init (&frame) != 0)
        return -1;

    const int rc = zlink_msg_recv (&frame, socket, flags);
    if (rc < 0) {
        const int err = zlink_errno ();
        zlink_msg_close (&frame);
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }

    size_t copied = 0;
    if (capture_bytes > 0 && !scratch.empty ()) {
        const size_t copy_size = std::min (
          std::min (capture_bytes, scratch.size ()),
          zlink_msg_size (&frame));
        if (copy_size > 0) {
            std::memcpy (scratch.data (), zlink_msg_data (&frame), copy_size);
            copied = copy_size;
        }
    }

    bool more = zlink_msg_more (&frame) != 0;
    zlink_msg_close (&frame);

    while (more) {
        zlink_msg_t next;
        if (zlink_msg_init (&next) != 0)
            return -1;

        int next_rc = zlink_msg_recv (&next, socket, 0);
        while (next_rc < 0 && zlink_errno () == EINTR)
            next_rc = zlink_msg_recv (&next, socket, 0);
        if (next_rc < 0) {
            zlink_msg_close (&next);
            return -1;
        }

        if (capture_bytes > copied && copied < scratch.size ()) {
            const size_t remain_capture = capture_bytes - copied;
            const size_t remain_scratch = scratch.size () - copied;
            const size_t next_copy_size = std::min (
              std::min (remain_capture, remain_scratch),
              zlink_msg_size (&next));
            if (next_copy_size > 0) {
                std::memcpy (
                  scratch.data () + copied,
                  zlink_msg_data (&next),
                  next_copy_size);
                copied += next_copy_size;
            }
        }

        more = zlink_msg_more (&next) != 0;
        zlink_msg_close (&next);
    }

    return 1;
}

inline int recv_one_message (void *socket, std::vector<char> &scratch, int flags)
{
    return recv_one_message (socket, scratch, flags, scratch.size ());
}

inline bool wait_all_client_connect_ready (std::vector<connect_monitor_t> &monitors,
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
  int client_socket_type,
  std::vector<void *> *sockets_out,
  std::vector<connect_monitor_t> *monitors_out)
{
    if (!sockets_out || !monitors_out)
        return false;

    sockets_out->assign (settings.clients, NULL);
    monitors_out->assign (settings.clients, connect_monitor_t ());

    for (size_t i = 0; i < sockets_out->size (); ++i) {
        void *sock = zlink_socket (ctx.get (), client_socket_type);
        if (!sock)
            return false;

        apply_benchmark_socket_options (sock, settings.hwm, transport);

        if (client_socket_type == ZLINK_ROUTER) {
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

        if (client_socket_type == ZLINK_SUB)
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
        (*sockets_out)[i] = sock;
    }

    return true;
}

inline void backoff_client_idle (const multi_bench_settings_t &settings)
{
    (void) settings;
    std::this_thread::yield ();
}

inline bool drain_socket_non_blocking (void *socket,
                                       std::vector<char> &scratch,
                                       long *recv_count,
                                       double *lat_sum,
                                       long *lat_count,
                                       bench_latency_sampler_t *lat_samples)
{
    if (!socket)
        return false;

    const bool collect_latency =
      lat_sum && lat_count && scratch.size () >= sizeof (unsigned long long);
    const size_t capture_bytes =
      collect_latency ? sizeof (unsigned long long) : 0;

    long local_recv = 0;
    while (true) {
        const int rc =
          recv_one_message (socket, scratch, ZLINK_DONTWAIT, capture_bytes);
        if (rc < 0)
            return false;
        if (rc == 0)
            break;

        ++local_recv;
        if (collect_latency) {
            unsigned long long sent_us = 0;
            std::memcpy (&sent_us, scratch.data (), sizeof (sent_us));
            const unsigned long long now_us = static_cast<unsigned long long> (
              std::chrono::duration_cast<std::chrono::microseconds> (
                std::chrono::system_clock::now ().time_since_epoch ())
                .count ());
            if (now_us >= sent_us) {
                const double sample_us = static_cast<double> (now_us - sent_us);
                *lat_sum += sample_us;
                (*lat_count)++;
                if (lat_samples)
                    lat_samples->add (sample_us);
            }
        }
    }

    if (recv_count)
        *recv_count += local_recv;
    return true;
}

inline bool run_one_way_window_loop (
  const std::vector<void *> &recv_sockets,
  const multi_bench_settings_t &settings,
  size_t scratch_capacity,
  double duration_seconds,
  bool collect_latency,
  long *recv_total,
  double *lat_sum,
  long *lat_count,
  bench_latency_stats_t *latency_stats)
{
    if (recv_sockets.empty ())
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

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::duration_cast<std::chrono::steady_clock::duration> (
        std::chrono::duration<double> (std::max (0.0, duration_seconds)));

    const int poll_timeout_ms = std::max (0, settings.client_poll_timeout_ms);
    const size_t scratch_size =
      std::max<size_t> (scratch_capacity, static_cast<size_t> (64));

    bool fatal_error = false;
    long recv_sum = 0;
    double lat_sum_local = 0.0;
    long lat_count_local = 0;
    bench_latency_sampler_t lat_samples;
    std::vector<char> scratch (scratch_size, '\0');
    std::vector<zlink_pollitem_t> poll_items (recv_sockets.size ());
    for (size_t i = 0; i < recv_sockets.size (); ++i) {
        const zlink_pollitem_t item = {
          recv_sockets[i],
          0,
          ZLINK_POLLIN,
          0,
        };
        poll_items[i] = item;
    }

    while (std::chrono::steady_clock::now () < deadline && !fatal_error) {
        bool progressed = false;

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

                long recv_now = 0;
                if (!drain_socket_non_blocking (
                      recv_sockets[i],
                      scratch,
                      &recv_now,
                      collect_latency ? &lat_sum_local : NULL,
                      collect_latency ? &lat_count_local : NULL,
                      collect_latency ? &lat_samples : NULL)) {
                    fatal_error = true;
                    break;
                }

                if (recv_now > 0) {
                    recv_sum += recv_now;
                    progressed = true;
                }
            }
        }

        if (fatal_error)
            break;

        if (!progressed)
            backoff_client_idle (settings);
    }

    if (recv_total)
        *recv_total = recv_sum;
    if (lat_sum)
        *lat_sum = lat_sum_local;
    if (lat_count)
        *lat_count = lat_count_local;
    if (latency_stats) {
        if (!collect_latency || lat_count_local <= 0) {
            *latency_stats = bench_latency_stats_t ();
        } else {
            bench_latency_stats_t merged = lat_samples.snapshot ();
            if (merged.mean_us <= 0.0)
                merged.mean_us =
              lat_sum_local / static_cast<double> (std::max<long> (1, lat_count_local));
            if (merged.p95_us <= 0.0)
                merged.p95_us = merged.mean_us;
            if (merged.p99_us <= 0.0)
                merged.p99_us = merged.p95_us;
            if (merged.p95_us < merged.mean_us)
                merged.p95_us = merged.mean_us;
            if (merged.p99_us < merged.p95_us)
                merged.p99_us = merged.p95_us;
            *latency_stats = merged;
        }
    }

    return !fatal_error;
}

inline bool run_one_way_duration (const std::vector<void *> &recv_sockets,
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

    if (recv_sockets.empty ())
        return false;

    if (!run_one_way_window_loop (
          recv_sockets,
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

    const double settle_seconds =
      static_cast<double> (std::max (0, settings.settle_ms)) / 1000.0;
    if (settle_seconds > 0.0) {
        if (!run_one_way_window_loop (
              recv_sockets,
              settings,
              scratch_capacity,
              settle_seconds,
              false,
              NULL,
              NULL,
              NULL,
              NULL)) {
            return false;
        }
    }

    long recv_count = 0;
    const bench_multi_cpu_sample_t sample_start = bench_multi_capture_cpu_sample ();
    if (!run_one_way_window_loop (
          recv_sockets,
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

    *throughput_out = static_cast<double> (recv_count)
                      / static_cast<double> (std::max (1, settings.duration_seconds));
    if (recv_count <= 0)
        return false;

    if (settle_seconds > 0.0) {
        if (!run_one_way_window_loop (
              recv_sockets,
              settings,
              scratch_capacity,
              settle_seconds,
              false,
              NULL,
              NULL,
              NULL,
              NULL)) {
            return false;
        }
    }

    double lat_sum = 0.0;
    long lat_count = 0;
    bench_latency_stats_t latency_stats;
    if (!run_one_way_window_loop (
          recv_sockets,
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
    if (lat_count <= 0)
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

    const double drain_seconds =
      static_cast<double> (std::max (0, settings.drain_ms)) / 1000.0;
    if (drain_seconds > 0.0) {
        if (!run_one_way_window_loop (
              recv_sockets,
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

    return true;
}

} // namespace perf_multi_client

#endif
