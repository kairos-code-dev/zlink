#ifndef PERF_CLIENT_HELPERS_HPP
#define PERF_CLIENT_HELPERS_HPP

#include "perf_common.hpp"
#include "perf_common_multi.hpp"
#include "perf_entry.hpp"
#include "perf_metric_header.hpp"
#include "../../../bench/with_zmq/multi/common/bench_resource.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace perf_client {

enum send_status_t
{
    send_ok = 0,
    send_blocked = 1,
    send_error = 2
};

struct echo_loop_state_t
{
    std::vector<uint8_t> awaiting_reply;
    size_t rr;
    uint64_t sequence;

    echo_loop_state_t () : rr (0), sequence (1) {}

    void reset (size_t socket_count)
    {
        awaiting_reply.assign (socket_count, 0);
        rr = 0;
        sequence = 1;
    }
};

inline size_t pending_reply_count (const echo_loop_state_t &state)
{
    size_t pending = 0;
    for (size_t i = 0; i < state.awaiting_reply.size (); ++i) {
        if (state.awaiting_reply[i] != 0)
            ++pending;
    }
    return pending;
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
        return send_blocked;
    return send_error;
}

inline send_status_t send_echo_message (void *socket,
                                        const std::string &server_id,
                                        std::vector<char> &payload,
                                        size_t payload_size,
                                        bool router_send)
{
    const int base_flags = ZLINK_DONTWAIT;

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

    if (capture_bytes > 0 && !scratch.empty ()) {
        const size_t copy_size = std::min (
          std::min (capture_bytes, scratch.size ()),
          zlink_msg_size (&frame));
        if (copy_size > 0)
            std::memcpy (scratch.data (), zlink_msg_data (&frame), copy_size);
    }

    zlink_msg_close (&frame);
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

    std::vector<char> ready (monitors.size (), 0);
    std::vector<size_t> active_indices;
    std::vector<zlink_pollitem_t> items;
    items.reserve (monitors.size ());
    active_indices.reserve (monitors.size ());

    size_t ready_count = 0;
    for (size_t i = 0; i < monitors.size (); ++i) {
        if (!monitors[i].monitor) {
            ready[i] = 1;
            ++ready_count;
            continue;
        }
        const zlink_pollitem_t item = {monitors[i].monitor, 0, ZLINK_POLLIN, 0};
        items.push_back (item);
        active_indices.push_back (i);
    }

    if (ready_count >= monitors.size ())
        return true;

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
        const int prc = zlink_poll (&items[0],
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
            const size_t monitor_index = active_indices[i];
            if (ready[monitor_index])
                continue;
            if ((items[i].revents & ZLINK_POLLIN) == 0)
                continue;
            if (poll_connect_ready_count (monitors[monitor_index]) <= 0)
                continue;
            ready[monitor_index] = 1;
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
  const bench_settings_t &settings,
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

        const bool monitor_opened = open_connect_monitor (sock, (*monitors_out)[i]);
        if (!monitor_opened) {
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

inline uint32_t next_metric_run_id ()
{
    static std::atomic<uint32_t> next_id (1);
    uint32_t run_id = next_id.fetch_add (1, std::memory_order_relaxed);
    if (run_id == 0)
        run_id = next_id.fetch_add (1, std::memory_order_relaxed);
    return run_id;
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

inline bool metric_header_matches (const perf_metric::header_t &header,
                                   uint32_t expected_run_id,
                                   perf_metric::phase_t expected_phase,
                                   size_t expected_msg_size)
{
    if (header.magic != perf_metric::k_magic)
        return false;
    if (header.phase != static_cast<uint32_t> (expected_phase))
        return false;
    if (header.msg_size != static_cast<uint32_t> (expected_msg_size))
        return false;
    if (expected_run_id != 0 && header.run_id != expected_run_id)
        return false;
    return true;
}

inline bool stamp_metric_payload (std::vector<char> &payload,
                                  size_t payload_size,
                                  uint32_t run_id,
                                  perf_metric::phase_t phase,
                                  size_t msg_size,
                                  uint64_t seq)
{
    if (payload_size < perf_metric::header_size ()
        || payload_size > payload.size ()) {
        return false;
    }

    return perf_metric::stamp_payload (
      payload.data (),
      payload_size,
      run_id,
      phase,
      msg_size,
      seq,
      perf_metric::now_us ());
}

inline size_t metric_capture_bytes ()
{
    return perf_metric::header_size () + static_cast<size_t> (64);
}

inline bool decode_metric_header_from_capture (
  const std::vector<char> &scratch,
  perf_metric::header_t *header_out)
{
    if (!header_out || scratch.size () < perf_metric::header_size ())
        return false;

    for (size_t offset = 0;
         (offset + perf_metric::header_size ()) <= scratch.size ();
         ++offset) {
        perf_metric::header_t candidate;
        if (!perf_metric::decode_header (
              scratch.data () + offset,
              scratch.size () - offset,
              &candidate)) {
            continue;
        }
        if (candidate.magic != perf_metric::k_magic)
            continue;
        *header_out = candidate;
        return true;
    }

    return false;
}

inline bool drain_socket_non_blocking (
  void *socket,
  std::vector<char> &scratch,
  size_t expected_msg_size,
  uint32_t expected_run_id,
  perf_metric::phase_t expected_phase,
  bool collect_latency,
  long *recv_count,
  double *lat_sum,
  long *lat_count,
  bench_latency_sampler_t *lat_samples)
{
    if (!socket)
        return false;

    long local_recv = 0;
    while (true) {
        const int rc = recv_one_message (
          socket, scratch, ZLINK_DONTWAIT, scratch.size ());
        if (rc < 0)
            return false;
        if (rc == 0)
            break;

        perf_metric::header_t header;
        if (!decode_metric_header_from_capture (scratch, &header))
            continue;

        if (header.magic != perf_metric::k_magic)
            continue;

        if (!metric_header_matches (
              header,
              expected_run_id,
              expected_phase,
              expected_msg_size)) {
            continue;
        }

        ++local_recv;
        if (collect_latency && lat_sum && lat_count) {
            const uint64_t now_us = perf_metric::now_us ();
            if (header.sent_ts_us > 0 && now_us >= header.sent_ts_us) {
                const double sample_us =
                  static_cast<double> (now_us - header.sent_ts_us);
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
  const bench_settings_t &settings,
  size_t expected_msg_size,
  uint32_t expected_run_id,
  perf_metric::phase_t expected_phase,
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
    const int effective_poll_timeout_ms = std::max (1, poll_timeout_ms);
    const size_t scratch_size =
      std::max<size_t> (scratch_capacity, metric_capture_bytes ());

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
        for (size_t i = 0; i < poll_items.size (); ++i)
            poll_items[i].revents = 0;

        const int prc = zlink_poll (
          &poll_items[0],
          static_cast<int> (poll_items.size ()),
          effective_poll_timeout_ms);
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
                      expected_msg_size,
                      expected_run_id,
                      expected_phase,
                      collect_latency,
                      &recv_now,
                      collect_latency ? &lat_sum_local : NULL,
                      collect_latency ? &lat_count_local : NULL,
                      collect_latency ? &lat_samples : NULL)) {
                    fatal_error = true;
                    break;
                }
                if (recv_now > 0)
                    recv_sum += recv_now;
            }
        }
    }

    if (recv_total)
        *recv_total = recv_sum;
    if (lat_sum)
        *lat_sum = lat_sum_local;
    if (lat_count)
        *lat_count = lat_count_local;
    if (latency_stats) {
        if (!collect_latency)
            *latency_stats = bench_latency_stats_t ();
        else
            normalize_latency_stats (
              lat_sum_local, lat_count_local, &lat_samples, latency_stats);
    }

    return !fatal_error;
}

inline bool run_one_way_duration (const std::vector<void *> &recv_sockets,
                                  const bench_settings_t &settings,
                                  size_t msg_size,
                                  uint32_t run_id,
                                  size_t scratch_capacity,
                                  double *throughput_out,
                                  bench_latency_stats_t *latency_out,
                                  bench_resource_metrics_t *metrics_out)
{
    if (!throughput_out || !latency_out || !metrics_out)
        return false;

    *throughput_out = 0.0;
    *latency_out = bench_latency_stats_t ();
    if (recv_sockets.empty ())
        return false;

    const double warmup_seconds =
      static_cast<double> (std::max (0, settings.warmup_seconds));
    if (warmup_seconds > 0.0
        && !run_one_way_window_loop (
          recv_sockets,
          settings,
          msg_size,
          run_id,
          perf_metric::phase_warmup,
          scratch_capacity,
          warmup_seconds,
          false,
          NULL,
          NULL,
          NULL,
          NULL)) {
        return false;
    }

    const double settle_seconds =
      static_cast<double> (std::max (0, settings.settle_ms)) / 1000.0;
    if (settle_seconds > 0.0
        && !run_one_way_window_loop (
          recv_sockets,
          settings,
          msg_size,
          run_id,
          perf_metric::phase_drain,
          scratch_capacity,
          settle_seconds,
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
    bench_latency_stats_t active_latency;

    const bench_cpu_sample_t sample_start = bench_capture_cpu_sample ();
    if (!run_one_way_window_loop (
          recv_sockets,
          settings,
          msg_size,
          run_id,
          perf_metric::phase_active,
          scratch_capacity,
          static_cast<double> (std::max (1, settings.duration_seconds)),
          true,
          &recv_count,
          &lat_sum,
          &lat_count,
          &active_latency)) {
        return false;
    }
    *metrics_out = bench_finish_resource_probe (sample_start);

    if (recv_count <= 0 || lat_count <= 0)
        return false;

    *throughput_out = static_cast<double> (recv_count)
                      / static_cast<double> (std::max (1, settings.duration_seconds));
    if (active_latency.mean_us <= 0.0)
        normalize_latency_stats (lat_sum, lat_count, NULL, &active_latency);
    *latency_out = active_latency;

    return true;
}

inline bool run_echo_window_round_robin (
  const std::vector<void *> &sockets,
  const bench_settings_t &settings,
  std::vector<char> &payload,
  size_t payload_size,
  size_t expected_msg_size,
  const std::string &server_id,
  bool client_router_send,
  uint32_t run_id,
  perf_metric::phase_t phase,
  size_t scratch_capacity,
  double duration_seconds,
  bool allow_send,
  bool collect_latency,
  long *recv_total,
  double *lat_sum,
  long *lat_count,
  bench_latency_stats_t *latency_stats,
  echo_loop_state_t *state_io = NULL)
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

    bool fatal_error = false;
    long local_recv = 0;
    double lat_sum_local = 0.0;
    long lat_count_local = 0;
    bench_latency_sampler_t lat_samples;

    echo_loop_state_t local_state;
    echo_loop_state_t *state = state_io ? state_io : &local_state;
    if (state->awaiting_reply.size () != sockets.size ())
        state->reset (sockets.size ());

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::duration_cast<std::chrono::steady_clock::duration> (
        std::chrono::duration<double> (std::max (0.0, duration_seconds)));

    const int poll_timeout_ms = std::max (0, settings.client_poll_timeout_ms);
    const int effective_poll_timeout_ms = std::max (1, poll_timeout_ms);
    const size_t scratch_size =
      std::max<size_t> (scratch_capacity, perf_metric::header_size ());

    std::vector<char> scratch (scratch_size, '\0');
    std::vector<std::vector<char> > slot_payloads (sockets.size (), payload);
    std::vector<uint8_t> send_pending (sockets.size (), 0);
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

    struct try_send_outcome_t
    {
        bool fatal;
        bool progressed;
    };

    const auto set_poll_events =
      [&] (size_t idx) {
        poll_items[idx].events =
          ZLINK_POLLIN | (send_pending[idx] != 0 ? ZLINK_POLLOUT : 0);
      };

    const auto try_send_slot =
      [&] (size_t idx) -> try_send_outcome_t {
        try_send_outcome_t outcome = {false, false};
        if (idx >= sockets.size () || state->awaiting_reply[idx] != 0)
            return outcome;
        if (slot_payloads[idx].size () < payload_size) {
            outcome.fatal = true;
            return outcome;
        }

        if (send_pending[idx] == 0) {
            if (!stamp_metric_payload (
                  slot_payloads[idx],
                  payload_size,
                  run_id,
                  phase,
                  expected_msg_size,
                  state->sequence++)) {
                outcome.fatal = true;
                return outcome;
            }
        }

        const send_status_t send_rc = send_echo_message (
          sockets[idx],
          server_id,
          slot_payloads[idx],
          payload_size,
          client_router_send);
        if (send_rc == send_ok) {
            send_pending[idx] = 0;
            set_poll_events (idx);
            state->awaiting_reply[idx] = 1;
            outcome.progressed = true;
            return outcome;
        }
        if (send_rc == send_blocked) {
            send_pending[idx] = 1;
            set_poll_events (idx);
            return outcome;
        }

        outcome.fatal = true;
        return outcome;
    };

    while (std::chrono::steady_clock::now () < deadline && !fatal_error) {
        if (allow_send) {
            const size_t start_rr = state->rr;
            for (size_t attempts = 0; attempts < sockets.size (); ++attempts) {
                const size_t idx = (start_rr + attempts) % sockets.size ();
                if (state->awaiting_reply[idx] != 0 || send_pending[idx] != 0)
                    continue;
                const try_send_outcome_t outcome = try_send_slot (idx);
                if (outcome.fatal) {
                    fatal_error = true;
                    break;
                }
            }
            state->rr = (start_rr + 1) % sockets.size ();
        }

        if (fatal_error)
            break;

        for (size_t i = 0; i < poll_items.size (); ++i)
            poll_items[i].revents = 0;

        const int prc = zlink_poll (
          &poll_items[0],
          static_cast<int> (poll_items.size ()),
          effective_poll_timeout_ms);
        if (prc < 0) {
            if (zlink_errno () != EINTR) {
                fatal_error = true;
                break;
            }
        } else if (prc > 0) {
            for (size_t i = 0; i < poll_items.size (); ++i) {
                if ((poll_items[i].revents & ZLINK_POLLIN) != 0) {
                    while (true) {
                        const int recv_rc = recv_one_message (
                          sockets[i],
                          scratch,
                          ZLINK_DONTWAIT,
                          scratch.size ());
                        if (recv_rc < 0) {
                            fatal_error = true;
                            break;
                        }
                        if (recv_rc <= 0)
                            break;

                        state->awaiting_reply[i] = 0;

                        perf_metric::header_t header;
                        if (decode_metric_header_from_capture (scratch, &header)
                            && metric_header_matches (
                              header, run_id, phase, expected_msg_size)) {
                            ++local_recv;
                            if (collect_latency) {
                                const uint64_t now_us = perf_metric::now_us ();
                                if (header.sent_ts_us > 0
                                    && now_us >= header.sent_ts_us) {
                                    const double sample_us =
                                      static_cast<double> (
                                        now_us - header.sent_ts_us)
                                      * 0.5;
                                    lat_sum_local += sample_us;
                                    lat_count_local++;
                                    lat_samples.add (sample_us);
                                }
                            }
                        }
                    }
                    if (fatal_error)
                        break;
                }

                if ((poll_items[i].revents & ZLINK_POLLOUT) != 0
                    && send_pending[i] != 0
                    && state->awaiting_reply[i] == 0) {
                    const try_send_outcome_t outcome = try_send_slot (i);
                    if (outcome.fatal) {
                        fatal_error = true;
                        break;
                    }
                }

                if (!allow_send || fatal_error)
                    continue;
                if (state->awaiting_reply[i] != 0 || send_pending[i] != 0)
                    continue;

                const try_send_outcome_t outcome = try_send_slot (i);
                if (outcome.fatal) {
                    fatal_error = true;
                    break;
                }
            }
        }
    }

    if (recv_total)
        *recv_total = local_recv;
    if (lat_sum)
        *lat_sum = lat_sum_local;
    if (lat_count)
        *lat_count = lat_count_local;
    if (latency_stats) {
        if (!collect_latency)
            *latency_stats = bench_latency_stats_t ();
        else
            normalize_latency_stats (
              lat_sum_local, lat_count_local, &lat_samples, latency_stats);
    }

    return !fatal_error;
}

inline bool run_echo_duration (
  const std::vector<void *> &sockets,
  const bench_settings_t &settings,
  std::vector<char> &payload,
  size_t payload_size,
  size_t msg_size,
  size_t scratch_capacity,
  const std::string &server_id,
  bool client_router_send,
  uint32_t run_id,
  double *throughput_out,
  bench_latency_stats_t *latency_out,
  bench_resource_metrics_t *metrics_out)
{
    if (!throughput_out || !latency_out || !metrics_out)
        return false;

    *throughput_out = 0.0;
    *latency_out = bench_latency_stats_t ();
    if (sockets.empty ())
        return false;

    echo_loop_state_t phase_state;
    phase_state.reset (sockets.size ());

    if (!run_echo_window_round_robin (
          sockets,
          settings,
          payload,
          payload_size,
          msg_size,
          server_id,
          client_router_send,
          run_id,
          perf_metric::phase_warmup,
          scratch_capacity,
          static_cast<double> (std::max (0, settings.warmup_seconds)),
          true,
          false,
          NULL,
          NULL,
          NULL,
          NULL,
          &phase_state)) {
        return false;
    }

    const double settle_seconds =
      static_cast<double> (std::max (0, settings.settle_ms)) / 1000.0;
    if (settle_seconds > 0.0
        && !run_echo_window_round_robin (
          sockets,
          settings,
          payload,
          payload_size,
          msg_size,
          server_id,
          client_router_send,
          run_id,
          perf_metric::phase_warmup,
          scratch_capacity,
          settle_seconds,
          false,
          false,
          NULL,
          NULL,
          NULL,
          NULL,
          &phase_state)) {
        return false;
    }

    if (pending_reply_count (phase_state) > 0) {
        const auto settle_deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::milliseconds (
            std::max (settings.connect_ready_timeout_ms,
                      std::max (100, settings.settle_ms)));
        while (pending_reply_count (phase_state) > 0) {
            const auto now = std::chrono::steady_clock::now ();
            if (now >= settle_deadline)
                return false;
            const double remain_seconds =
              std::chrono::duration_cast<std::chrono::duration<double> > (
                settle_deadline - now)
                .count ();
            const double slice_seconds = std::min (0.05, remain_seconds);
            if (slice_seconds <= 0.0)
                return false;
            if (!run_echo_window_round_robin (
                  sockets,
                  settings,
                  payload,
                  payload_size,
                  msg_size,
                  server_id,
                  client_router_send,
                  run_id,
                  perf_metric::phase_warmup,
                  scratch_capacity,
                  slice_seconds,
                  false,
                  false,
                  NULL,
                  NULL,
                  NULL,
                  NULL,
                  &phase_state)) {
                return false;
            }
        }
    }

    long recv_count = 0;
    double lat_sum = 0.0;
    long lat_count = 0;
    bench_latency_stats_t active_latency;

    const bench_cpu_sample_t sample_start = bench_capture_cpu_sample ();
    if (!run_echo_window_round_robin (
          sockets,
          settings,
          payload,
          payload_size,
          msg_size,
          server_id,
          client_router_send,
          run_id,
          perf_metric::phase_active,
          scratch_capacity,
          static_cast<double> (std::max (1, settings.duration_seconds)),
          true,
          true,
          &recv_count,
          &lat_sum,
          &lat_count,
          &active_latency,
          &phase_state)) {
        return false;
    }
    *metrics_out = bench_finish_resource_probe (sample_start);

    if (recv_count <= 0 || lat_count <= 0)
        return false;

    *throughput_out = static_cast<double> (recv_count)
                      / static_cast<double> (std::max (1, settings.duration_seconds));
    if (active_latency.mean_us <= 0.0)
        normalize_latency_stats (lat_sum, lat_count, NULL, &active_latency);
    *latency_out = active_latency;

    return true;
}

inline void print_client_resource_result_lines (
  const char *pattern,
  const std::string &lib_name,
  const std::string &transport,
  size_t msg_size,
  const bench_resource_metrics_t &metrics)
{
    if (metrics.has_cpu_pct) {
        std::cout << "RESULT," << lib_name << "," << pattern << ","
                  << transport << "," << msg_size << ",client_cpu_pct,"
                  << std::fixed << std::setprecision (2) << metrics.cpu_pct
                  << std::endl;
    }

    if (metrics.has_mem_mb) {
        std::cout << "RESULT," << lib_name << "," << pattern << ","
                  << transport << "," << msg_size << ",client_mem_mb,"
                  << std::fixed << std::setprecision (2) << metrics.mem_mb
                  << std::endl;
    }
}

inline void print_client_result_lines (
  const char *pattern,
  const std::string &lib_name,
  const std::string &transport,
  size_t msg_size,
  double throughput,
  const bench_latency_stats_t &latency,
  const bench_resource_metrics_t &metrics)
{
    print_result (
      lib_name,
      pattern,
      transport,
      msg_size,
      throughput,
      latency.mean_us,
      latency.p95_us,
      latency.p99_us);

    print_client_resource_result_lines (
      pattern, lib_name, transport, msg_size, metrics);
}

inline void print_echo_client_result_lines (
  const char *pattern,
  const std::string &lib_name,
  const std::string &transport,
  size_t msg_size,
  double throughput,
  const bench_latency_stats_t &latency,
  const bench_resource_metrics_t &metrics)
{
    print_client_result_lines (
      pattern,
      lib_name,
      transport,
      msg_size,
      throughput,
      latency,
      metrics);
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

inline int run_echo_client_benchmark (
  const char *pattern,
  int client_socket_type,
  bool client_router_send,
  const char *server_routing_id,
  const std::string &lib_name,
  const std::string &transport,
  const std::string &endpoint,
  size_t fallback_size)
{
    set_perf_pattern_env (pattern);

    if (!is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << pattern << ","
                  << transport << std::endl;
        return 0;
    }

    if (!transport_available (transport)) {
        std::cerr << "transport unavailable: " << transport << std::endl;
        return 1;
    }

    const bench_settings_t settings = resolve_bench_settings ();
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
          settings,
          client_socket_type,
          &sockets,
          &monitors)) {
        close_client_monitors (&monitors);
        close_client_sockets (&sockets);
        return 1;
    }

    if (!wait_all_client_connect_ready (monitors, settings.connect_ready_timeout_ms)) {
        close_client_monitors (&monitors);
        close_client_sockets (&sockets);
        return 1;
    }
    close_client_monitors (&monitors);

    const std::string server_id = server_routing_id ? server_routing_id : "SERVER";
    const size_t payload_capacity =
      std::max<size_t> (max_msg_size, perf_metric::header_size ());
    const size_t scratch_capacity = metric_capture_bytes ();
    std::vector<char> payload (payload_capacity, 'c');

    for (size_t si = 0; si < msg_sizes.size (); ++si) {
        const size_t msg_size = msg_sizes[si];
        const size_t payload_size =
          std::max<size_t> (msg_size, perf_metric::header_size ());
        const uint32_t run_id = next_metric_run_id ();
        double throughput = 0.0;
        bench_latency_stats_t latency;
        bench_resource_metrics_t metrics;

        if (!run_echo_duration (
              sockets,
              settings,
              payload,
              payload_size,
              msg_size,
              scratch_capacity,
              server_id,
              client_router_send,
              run_id,
              &throughput,
              &latency,
              &metrics)) {
            close_client_sockets (&sockets);
            return 1;
        }

        print_echo_client_result_lines (
          pattern, lib_name, transport, msg_size, throughput, latency, metrics);
    }

    close_client_sockets (&sockets);
    return 0;
}

} // namespace perf_client

#endif
