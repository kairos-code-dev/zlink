#ifndef BENCH_MULTI_CLIENT_HPP
#define BENCH_MULTI_CLIENT_HPP

#include "bench_multi_pattern.hpp"
#include "bench_multi_resource.hpp"
#include "bench_common_multi.hpp"
#include "bench_common_zlink.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace bench_multi_client_detail {

inline bool set_env_pattern_name (const char *pattern)
{
#if defined(_WIN32)
    if (!pattern)
        return false;
    return _putenv_s ("BENCH_MULTI_PATTERN", pattern) == 0;
#else
    if (!pattern)
        return false;
    return setenv ("BENCH_MULTI_PATTERN", pattern, 1) == 0;
#endif
}

inline unsigned long long wallclock_now_us ()
{
    return static_cast<unsigned long long> (
      std::chrono::duration_cast<std::chrono::microseconds> (
        std::chrono::system_clock::now ().time_since_epoch ())
        .count ());
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

inline bool is_transient_send_error (int err)
{
    return err == EAGAIN || err == EINTR;
}

inline multi_send_result_t classify_send_result (int rc)
{
    if (rc >= 0)
        return multi_send_ok;
    const int err = zlink_errno ();
    if (is_transient_send_error (err))
        return multi_send_would_block;
    if (err == ETERM || err == ENOTSOCK)
        return multi_send_error;
    return multi_send_error;
}

inline int recv_one_message (void *socket,
                             std::vector<char> &scratch,
                             int flags)
{
    zlink_routing_id_t source_rid;
    source_rid.size = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    const int rc = recv_message_parts (
      socket, &source_rid, &parts, &part_count,
      static_cast<zlink_send_flags_t> (flags));
    if (rc < 0) {
        const int err = zlink_errno ();
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }
    if (parts) {
        zlink_multipart_close (parts, part_count);
        free (parts);
    }

    return 1;
}

inline multi_send_result_t send_echo_message (void *socket,
                                              const multi_pattern_config_t &cfg,
                                              const std::string &server_id,
                                              const std::vector<char> &payload,
                                              size_t payload_size,
                                              bool blocking)
{
    const zlink_send_flags_t base_flags =
      blocking ? 0 : static_cast<zlink_send_flags_t> (ZLINK_DONTWAIT);

    if (cfg.client_router_send) {
        zlink_msg_t parts[2];
        if (zlink_msg_init_size (&parts[0], server_id.size ()) != 0)
            return multi_send_error;
        if (zlink_msg_init_size (&parts[1], payload_size) != 0) {
            zlink_msg_close (&parts[0]);
            return multi_send_error;
        }
        if (!server_id.empty ())
            std::memcpy (
              zlink_msg_data (&parts[0]), server_id.data (), server_id.size ());
        if (payload_size > 0)
            std::memcpy (
              zlink_msg_data (&parts[1]), payload.data (), payload_size);
        const int id_rc = ::zlink_send (socket, parts, 2, base_flags);
        const multi_send_result_t id_status = classify_send_result (id_rc);
        if (id_status != multi_send_ok)
            return id_status;
        return id_status;
    }

    zlink_msg_t part;
    if (zlink_msg_init_size (&part, payload_size) != 0)
        return multi_send_error;
    if (payload_size > 0)
        std::memcpy (zlink_msg_data (&part), payload.data (), payload_size);
    const int payload_rc = ::zlink_send (socket, &part, 1, base_flags);
    return classify_send_result (payload_rc);
}

inline bool recv_with_poll (const std::vector<void *> &sockets,
                            std::vector<int> &pending,
                            std::vector<zlink_pollitem_t> &poll_items,
                            std::vector<char> &scratch,
                            int recv_batch,
                            int poll_timeout_ms,
                            long *recv_total)
{
    if (sockets.empty ())
        return true;

    if (poll_items.size () != sockets.size ()) {
        poll_items.resize (sockets.size ());
        for (size_t i = 0; i < sockets.size (); ++i) {
            const zlink_pollitem_t item = {sockets[i], 0, ZLINK_POLLIN, 0};
            poll_items[i] = item;
        }
    }

    for (size_t i = 0; i < poll_items.size (); ++i)
        poll_items[i].revents = 0;

    const int prc = zlink_poll (
      &poll_items[0],
      static_cast<int> (poll_items.size ()),
      poll_timeout_ms);
    if (prc < 0)
        return zlink_errno () == EINTR;
    if (prc == 0)
        return true;

    int received = 0;
    for (size_t i = 0; i < poll_items.size () && received < recv_batch; ++i) {
        if ((poll_items[i].revents & ZLINK_POLLIN) == 0)
            continue;

        while (received < recv_batch) {
            const int rc = recv_one_message (
              sockets[i],
              scratch,
              ZLINK_DONTWAIT);
            if (rc < 0)
                return false;
            if (rc == 0)
                break;

            ++received;
            if (pending[i] > 0)
                --pending[i];
            if (recv_total)
                (*recv_total)++;
        }
    }

    return true;
}

inline bool run_echo_throughput_window (
  const std::vector<void *> &sockets,
  const multi_pattern_config_t &cfg,
  const multi_bench_settings_t &settings,
  const std::vector<char> &payload,
  size_t payload_size,
  const std::string &server_id,
  std::vector<int> &pending,
  std::vector<zlink_pollitem_t> &poll_items,
  std::vector<char> &scratch,
  double duration_seconds,
  bool allow_send,
  long *recv_total)
{
    if (sockets.empty ())
        return false;

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::duration_cast<std::chrono::steady_clock::duration> (
        std::chrono::duration<double> (std::max (0.0, duration_seconds)));

    const int inflight_per_socket = std::max (1, settings.inflight);
    size_t rr = 0;
    long send_ok_count = 0;
    long send_would_block_count = 0;
    long send_error_count = 0;

    while (std::chrono::steady_clock::now () < deadline) {
        if (allow_send) {
            const size_t send_attempts =
              std::max<size_t> (1, std::min<size_t> (sockets.size (), 1024));
            for (size_t a = 0; a < send_attempts; ++a) {
                const size_t idx = rr % sockets.size ();
                ++rr;
                if (pending[idx] >= inflight_per_socket)
                    continue;

                const multi_send_result_t send_rc = send_echo_message (
                  sockets[idx],
                  cfg,
                  server_id,
                  payload,
                  payload_size,
                  false);
                if (send_rc == multi_send_ok) {
                    pending[idx]++;
                    ++send_ok_count;
                    continue;
                }
                if (send_rc == multi_send_would_block) {
                    ++send_would_block_count;
                    continue;
                }
                if (send_rc == multi_send_error) {
                    ++send_error_count;
                    if (bench_debug_enabled ()) {
                        std::cerr << cfg.pattern
                                  << ": send_echo_message failed (transport window), socket_index="
                                  << idx << ", errno=" << zlink_errno () << " ("
                                  << zlink_strerror (zlink_errno ()) << ")"
                                  << std::endl;
                    }
                    return false;
                }
            }
        }

        if (!recv_with_poll (
              sockets,
              pending,
              poll_items,
              scratch,
              settings.recv_batch,
              1,
              recv_total)) {
            if (bench_debug_enabled ()) {
                std::cerr << cfg.pattern
                          << ": recv_with_poll failed during throughput window"
                          << std::endl;
            }
            return false;
        }
    }

    if (bench_debug_enabled ()) {
        std::cerr << cfg.pattern << ": throughput window summary"
                  << " send_ok=" << send_ok_count
                  << " send_would_block=" << send_would_block_count
                  << " send_error=" << send_error_count;
        if (recv_total)
            std::cerr << " recv_total=" << *recv_total;
        std::cerr << std::endl;
    }

    return true;
}

inline bool drain_echo_inflight (const std::vector<void *> &sockets,
                                 const multi_bench_settings_t &settings,
                                 std::vector<int> &pending,
                                 std::vector<zlink_pollitem_t> &poll_items,
                                 std::vector<char> &scratch)
{
    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (std::max (0, settings.drain_ms));

    while (std::chrono::steady_clock::now () < deadline) {
        long pending_total = 0;
        for (size_t i = 0; i < pending.size (); ++i)
            pending_total += pending[i];
        if (pending_total <= 0)
            return true;

        if (!recv_with_poll (
              sockets,
              pending,
              poll_items,
              scratch,
              settings.recv_batch,
              5,
              NULL)) {
            return false;
        }
    }

    return true;
}

inline double measure_echo_latency_us (void *socket,
                                       const multi_pattern_config_t &cfg,
                                       const std::string &server_id,
                                       const std::vector<char> &payload,
                                       size_t payload_size,
                                       std::vector<char> &scratch)
{
    if (!socket)
        return 0.0;

    const int lat_count = std::max (1, resolve_bench_count ("BENCH_LAT_COUNT", 200));
    const int per_round_timeout_ms = std::max (
      10, resolve_multi_int_env ("BENCH_MULTI_LAT_TIMEOUT_MS", 200, 10));
    int completed = 0;

    stopwatch_t sw;
    sw.start ();

    zlink_pollitem_t item[] = {{socket, 0, ZLINK_POLLIN, 0}};
    for (int i = 0; i < lat_count; ++i) {
        bool sent = false;
        const auto send_deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::milliseconds (per_round_timeout_ms);
        while (std::chrono::steady_clock::now () < send_deadline) {
            const multi_send_result_t send_rc = send_echo_message (
              socket,
              cfg,
              server_id,
              payload,
              payload_size,
              false);
            if (send_rc == multi_send_ok) {
                sent = true;
                break;
            }
            if (send_rc == multi_send_error)
                return completed > 0
                         ? (sw.elapsed_ms () * 1000.0)
                             / (completed * 2.0)
                         : 0.0;
            std::this_thread::sleep_for (std::chrono::microseconds (100));
        }
        if (!sent)
            break;

        bool received = false;
        const auto recv_deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::milliseconds (per_round_timeout_ms);
        while (std::chrono::steady_clock::now () < recv_deadline) {
            item[0].revents = 0;
            const int prc = zlink_poll (item, 1, 1);
            if (prc < 0) {
                if (zlink_errno () == EINTR)
                    continue;
                break;
            }
            if (prc == 0 || (item[0].revents & ZLINK_POLLIN) == 0)
                continue;

            const int rc = recv_one_message (socket, scratch, ZLINK_DONTWAIT);
            if (rc < 0)
                break;
            if (rc == 0)
                continue;
            received = true;
            break;
        }

        if (!received)
            break;
        ++completed;
    }

    if (completed <= 0)
        return 0.0;
    const double divisor = cfg.one_way_latency ? completed : (completed * 2.0);
    return (sw.elapsed_ms () * 1000.0) / divisor;
}

inline double measure_echo_latency_pool_us (
  const std::vector<void *> &sockets,
  const multi_pattern_config_t &cfg,
  const std::string &server_id,
  const std::vector<char> &payload,
  size_t payload_size,
  std::vector<int> &pending,
  std::vector<zlink_pollitem_t> &poll_items,
  std::vector<char> &scratch)
{
    if (sockets.empty ())
        return 0.0;

    const int lat_count = std::max (1, resolve_bench_count ("BENCH_LAT_COUNT", 200));
    const int per_round_timeout_ms = std::max (
      10, resolve_multi_int_env ("BENCH_MULTI_LAT_TIMEOUT_MS", 200, 10));
    int completed = 0;
    long recv_total = 0;
    size_t rr = 0;

    stopwatch_t sw;
    sw.start ();

    for (int i = 0; i < lat_count; ++i) {
        bool sent = false;
        const auto send_deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::milliseconds (per_round_timeout_ms);

        while (std::chrono::steady_clock::now () < send_deadline) {
            const size_t idx = rr % sockets.size ();
            ++rr;
            const multi_send_result_t send_rc = send_echo_message (
              sockets[idx],
              cfg,
              server_id,
              payload,
              payload_size,
              false);
            if (send_rc == multi_send_ok) {
                pending[idx]++;
                sent = true;
                break;
            }
            if (send_rc == multi_send_error) {
                const double divisor =
                  cfg.one_way_latency ? completed : (completed * 2.0);
                return completed > 0 ? (sw.elapsed_ms () * 1000.0) / divisor : 0.0;
            }
            std::this_thread::sleep_for (std::chrono::microseconds (100));
        }
        if (!sent)
            break;

        bool received = false;
        const long before_recv = recv_total;
        const auto recv_deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::milliseconds (per_round_timeout_ms);

        while (std::chrono::steady_clock::now () < recv_deadline) {
            if (!recv_with_poll (
                  sockets,
                  pending,
                  poll_items,
                  scratch,
                  1,
                  1,
                  &recv_total)) {
                const double divisor =
                  cfg.one_way_latency ? completed : (completed * 2.0);
                return completed > 0 ? (sw.elapsed_ms () * 1000.0) / divisor : 0.0;
            }
            if (recv_total > before_recv) {
                received = true;
                break;
            }
        }

        if (!received)
            break;
        ++completed;
    }

    if (completed <= 0)
        return 0.0;
    const double divisor = cfg.one_way_latency ? completed : (completed * 2.0);
    return (sw.elapsed_ms () * 1000.0) / divisor;
}

inline bool create_client_sockets (
  ctx_guard_t &ctx,
  const std::string &transport,
  const std::string &endpoint,
  const multi_pattern_config_t &cfg,
  const multi_bench_settings_t &settings,
  std::vector<void *> *sockets_out,
  std::vector<connect_monitor_t> *monitors_out)
{
    if (!sockets_out)
        return false;

    sockets_out->assign (settings.clients, NULL);
    if (monitors_out)
        monitors_out->assign (settings.clients, connect_monitor_t ());

    const int linger_ms = 0;
    for (size_t i = 0; i < sockets_out->size (); ++i) {
        void *sock = zlink_socket (
          ctx.get (), static_cast<zlink_socket_type_t> (cfg.client_socket_type));
        if (!sock) {
            if (bench_debug_enabled ()) {
                std::cerr << cfg.pattern
                          << ": zlink_socket failed at client_index=" << i
                          << ", errno=" << zlink_errno () << " ("
                          << zlink_strerror (zlink_errno ()) << ")"
                          << std::endl;
            }
            return false;
        }

        set_sockopt_int (sock, ZLINK_OPT_LINGER, linger_ms,
                         "ZLINK_OPT_LINGER");
        apply_benchmark_hwm (sock, settings.hwm);

        if (cfg.client_socket_type == ZLINK_SOCKET_ROUTER) {
            char id_buf[32];
            const int id_len =
              std::snprintf (id_buf, sizeof (id_buf), "client_%zu", i);
            if (id_len > 0) {
                zlink_set_routing_id (sock, id_buf,
                                      static_cast<size_t> (id_len));
            }
        }

        if (cfg.client_socket_type == ZLINK_SOCKET_SUB) {
            zlink_set_subscription (sock, "");
        }

        if (!setup_tls_client (sock, transport)) {
            if (bench_debug_enabled ()) {
                std::cerr << cfg.pattern
                          << ": setup_tls_client failed at client_index=" << i
                          << std::endl;
            }
            zlink_close (sock);
            return false;
        }

        if (monitors_out
            && !open_connect_monitor (sock, (*monitors_out)[i])) {
            if (bench_debug_enabled ()) {
                std::cerr << cfg.pattern
                          << ": open_connect_monitor failed at client_index="
                          << i << ", errno=" << zlink_errno () << " ("
                          << zlink_strerror (zlink_errno ()) << ")"
                          << std::endl;
            }
            zlink_close (sock);
            return false;
        }

        if (!connect_checked (sock, endpoint, transport)) {
            if (bench_debug_enabled ()) {
                std::cerr << cfg.pattern
                          << ": connect_checked failed at client_index=" << i
                          << std::endl;
            }
            if (monitors_out)
                close_connect_monitor ((*monitors_out)[i]);
            zlink_close (sock);
            return false;
        }

        (*sockets_out)[i] = sock;
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

inline void print_client_result_lines (
  const std::string &lib_name,
  const multi_pattern_config_t &cfg,
  const std::string &transport,
  size_t size,
  double throughput,
  double latency,
  const bench_multi_resource_metrics_t &metrics)
{
    const bool is_echo_pattern =
      std::strcmp (cfg.pattern, "MULTI_DEALER_DEALER") == 0
      || std::strcmp (cfg.pattern, "MULTI_DEALER_ROUTER") == 0
      || std::strcmp (cfg.pattern, "MULTI_ROUTER_ROUTER") == 0;
    const double direction_factor = is_echo_pattern ? 2.0 : 1.0;
    const double bandwidth_mb_s =
      (throughput * static_cast<double> (size) * direction_factor) / 1000000.0;
    std::cout << "RESULT," << lib_name << "," << cfg.pattern << "," << transport
              << "," << size << ",throughput," << std::fixed
              << std::setprecision (2) << throughput << std::endl;
    std::cout << "RESULT," << lib_name << "," << cfg.pattern << "," << transport
              << "," << size << ",bandwidth," << std::fixed
              << std::setprecision (2) << bandwidth_mb_s << std::endl;
    std::cout << "RESULT," << lib_name << "," << cfg.pattern << "," << transport
              << "," << size << ",latency," << std::fixed
              << std::setprecision (2) << latency << std::endl;

    if (metrics.has_cpu_pct) {
        std::cout << "RESULT," << lib_name << "," << cfg.pattern << ","
                  << transport << "," << size << ",client_cpu_pct,"
                  << std::fixed << std::setprecision (2) << metrics.cpu_pct
                  << std::endl;
    }

    if (metrics.has_mem_mb) {
        std::cout << "RESULT," << lib_name << "," << cfg.pattern << ","
                  << transport << "," << size << ",client_mem_mb,"
                  << std::fixed << std::setprecision (2) << metrics.mem_mb
                  << std::endl;
    }
}

inline bool run_pubsub_duration (
  const std::vector<void *> &subs,
  const multi_bench_settings_t &settings,
  std::vector<zlink_pollitem_t> &poll_items,
  std::vector<char> &scratch,
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

    if (poll_items.size () != subs.size ()) {
        poll_items.resize (subs.size ());
        for (size_t i = 0; i < subs.size (); ++i) {
            const zlink_pollitem_t item = {subs[i], 0, ZLINK_POLLIN, 0};
            poll_items[i] = item;
        }
    }

    auto recv_loop = [&] (double seconds,
                          bool collect,
                          long *recv_count,
                          double *lat_sum,
                          long *lat_count) {
        const auto deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::duration_cast<std::chrono::steady_clock::duration> (
            std::chrono::duration<double> (std::max (0.0, seconds)));

        while (std::chrono::steady_clock::now () < deadline) {
            for (size_t i = 0; i < poll_items.size (); ++i)
                poll_items[i].revents = 0;

            const int prc = zlink_poll (
              &poll_items[0],
              static_cast<int> (poll_items.size ()),
              1);
            if (prc < 0) {
                if (zlink_errno () == EINTR)
                    continue;
                return false;
            }
            if (prc == 0)
                continue;

            for (size_t i = 0; i < poll_items.size (); ++i) {
                if ((poll_items[i].revents & ZLINK_POLLIN) == 0)
                    continue;

                while (true) {
                    const int rc = recv_one_message (
                      subs[i],
                      scratch,
                      ZLINK_DONTWAIT);
                    if (rc < 0)
                        return false;
                    if (rc == 0)
                        break;

                    if (recv_count)
                        (*recv_count)++;

                    if (collect && lat_sum && lat_count
                        && scratch.size () >= sizeof (unsigned long long)) {
                        unsigned long long sent_us = 0;
                        std::memcpy (
                          &sent_us,
                          scratch.data (),
                          sizeof (sent_us));
                        const unsigned long long now_us = wallclock_now_us ();
                        if (now_us >= sent_us) {
                            *lat_sum += static_cast<double> (now_us - sent_us);
                            (*lat_count)++;
                        }
                    }
                }
            }
        }

        return true;
    };

    if (!recv_loop (
          static_cast<double> (std::max (0, settings.warmup_seconds)),
          false,
          NULL,
          NULL,
          NULL)) {
        return false;
    }

    if (settings.settle_ms > 0)
        std::this_thread::sleep_for (
          std::chrono::milliseconds (settings.settle_ms));

    long recv_count = 0;
    double lat_sum = 0.0;
    long lat_count = 0;

    const bench_multi_cpu_sample_t sample_start = bench_multi_capture_cpu_sample ();
    if (!recv_loop (
          static_cast<double> (std::max (1, settings.duration_seconds)),
          true,
          &recv_count,
          &lat_sum,
          &lat_count)) {
        return false;
    }

    *metrics_out = bench_multi_finish_resource_probe (sample_start);

    if (settings.drain_ms > 0) {
        if (!recv_loop (
              static_cast<double> (settings.drain_ms) / 1000.0,
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
  const multi_pattern_config_t &cfg,
  const multi_bench_settings_t &settings,
  const std::vector<char> &payload,
  size_t payload_size,
  const std::string &server_id,
  std::vector<int> &pending,
  std::vector<zlink_pollitem_t> &poll_items,
  std::vector<char> &scratch,
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

    if (!run_echo_throughput_window (
          sockets,
          cfg,
          settings,
          payload,
          payload_size,
          server_id,
          pending,
          poll_items,
          scratch,
          static_cast<double> (std::max (0, settings.warmup_seconds)),
          true,
          NULL)) {
        if (bench_debug_enabled ()) {
            std::cerr << cfg.pattern
                      << ": warmup throughput window failed" << std::endl;
        }
        return false;
    }

    if (settings.settle_ms > 0)
        std::this_thread::sleep_for (
          std::chrono::milliseconds (settings.settle_ms));

    long recv_count = 0;
    const bench_multi_cpu_sample_t sample_start = bench_multi_capture_cpu_sample ();
    if (!run_echo_throughput_window (
          sockets,
          cfg,
          settings,
          payload,
          payload_size,
          server_id,
          pending,
          poll_items,
          scratch,
          static_cast<double> (std::max (1, settings.duration_seconds)),
          true,
          &recv_count)) {
        if (bench_debug_enabled ()) {
            std::cerr << cfg.pattern
                      << ": duration throughput window failed" << std::endl;
        }
        return false;
    }

    *metrics_out = bench_multi_finish_resource_probe (sample_start);

    if (!drain_echo_inflight (sockets, settings, pending, poll_items, scratch)) {
        if (bench_debug_enabled ()) {
            std::cerr << cfg.pattern
                      << ": inflight drain failed after throughput phase"
                      << std::endl;
        }
        return false;
    }

    *throughput_out = static_cast<double> (recv_count)
                      / static_cast<double> (std::max (1, settings.duration_seconds));
    if (recv_count <= 0) {
        if (bench_debug_enabled ()) {
            std::cerr << cfg.pattern
                      << ": recv_count <= 0 after throughput phase"
                      << std::endl;
        }
        return false;
    }

    const int latency_retries = std::max (
      1, resolve_multi_int_env ("BENCH_MULTI_LAT_RETRIES", 3, 1));
    const int latency_retry_drain_ms = std::max (
      0, resolve_multi_int_env ("BENCH_MULTI_LAT_RETRY_DRAIN_MS", 100, 0));
    const bool pooled_latency_pattern =
      !cfg.server_router_echo && !cfg.client_router_send;

    double measured_latency = 0.0;
    for (int attempt = 0; attempt < latency_retries; ++attempt) {
        if (pooled_latency_pattern) {
            measured_latency = measure_echo_latency_pool_us (
              sockets,
              cfg,
              server_id,
              payload,
              payload_size,
              pending,
              poll_items,
              scratch);
        } else {
            measured_latency = measure_echo_latency_us (
              sockets[0],
              cfg,
              server_id,
              payload,
              payload_size,
              scratch);
            if (measured_latency <= 0.0) {
                measured_latency = measure_echo_latency_pool_us (
                  sockets,
                  cfg,
                  server_id,
                  payload,
                  payload_size,
                  pending,
                  poll_items,
                  scratch);
            }
        }
        if (measured_latency > 0.0)
            break;

        if (attempt + 1 < latency_retries) {
            multi_bench_settings_t retry_settings = settings;
            retry_settings.drain_ms =
              std::max (retry_settings.drain_ms, latency_retry_drain_ms);
            if (!drain_echo_inflight (
                  sockets,
                  retry_settings,
                  pending,
                  poll_items,
                  scratch)) {
                return false;
            }
        }
    }

    if (measured_latency <= 0.0)
    {
        if (bench_debug_enabled ()) {
            std::cerr << cfg.pattern
                      << ": measured_latency <= 0 after retries="
                      << latency_retries << std::endl;
        }
        return false;
    }

    *latency_out = measured_latency;
    return true;
}

} // namespace bench_multi_client_detail

inline int run_multi_client_main (int argc,
                                  char **argv,
                                  const multi_pattern_config_t &cfg)
{
    if (argc < 4)
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t fallback_size =
      static_cast<size_t> (std::strtoull (argv[3], NULL, 10));

    std::string endpoint;
    if (!bench_multi_client_detail::parse_endpoint_arg (
          argc,
          argv,
          &endpoint)) {
        std::cerr << "missing --endpoint" << std::endl;
        return 1;
    }

    if (!transport_available (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << cfg.pattern << ","
                  << transport << std::endl;
        return 0;
    }

    bench_multi_client_detail::set_env_pattern_name (cfg.pattern);
    const multi_bench_settings_t base_settings = resolve_multi_bench_settings ();
    const std::vector<size_t> msg_sizes = resolve_bench_msg_sizes (fallback_size);
    size_t max_msg_size = fallback_size;
    for (size_t i = 0; i < msg_sizes.size (); ++i) {
        if (msg_sizes[i] > max_msg_size)
            max_msg_size = msg_sizes[i];
    }

    ctx_guard_t ctx;
    if (!ctx.valid ())
        return 1;

    std::vector<void *> sockets;
    std::vector<connect_monitor_t> monitors;
    if (!bench_multi_client_detail::create_client_sockets (
          ctx,
          transport,
          endpoint,
          cfg,
          base_settings,
          &sockets,
          &monitors)) {
        if (bench_debug_enabled ()) {
            std::cerr << cfg.pattern
                      << ": create_client_sockets failed" << std::endl;
        }
        bench_multi_client_detail::close_client_monitors (&monitors);
        bench_multi_client_detail::close_client_sockets (&sockets);
        return 1;
    }

    if (!bench_multi_client_detail::wait_all_client_connect_ready (
          monitors,
          base_settings.connect_ready_timeout_ms)) {
        if (bench_debug_enabled ()) {
            std::cerr << cfg.pattern
                      << ": connect_ready wait timeout (expected="
                      << monitors.size () << ", timeout_ms="
                      << base_settings.connect_ready_timeout_ms << ")"
                      << std::endl;
        }
        bench_multi_client_detail::close_client_monitors (&monitors);
        bench_multi_client_detail::close_client_sockets (&sockets);
        return 1;
    }
    bench_multi_client_detail::close_client_monitors (&monitors);

    const std::string server_id =
      cfg.server_routing_id ? cfg.server_routing_id : "SERVER";
    const size_t payload_capacity = std::max<size_t> (max_msg_size, 64);
    std::vector<char> payload (payload_capacity, 'c');
    std::vector<char> scratch (
      std::max<size_t> (payload_capacity + 256, static_cast<size_t> (1024)),
      '\0');
    std::vector<int> pending (sockets.size (), 0);
    std::vector<zlink_pollitem_t> poll_items;

    for (size_t si = 0; si < msg_sizes.size (); ++si) {
        const size_t msg_size = msg_sizes[si];
        multi_bench_settings_t settings = base_settings;
        settings.inflight =
          resolve_multi_inflight_for_size (base_settings, msg_size);
        const size_t payload_size = std::max<size_t> (msg_size, 64);
        std::fill (pending.begin (), pending.end (), 0);

        double throughput = 0.0;
        double latency = 0.0;
        bench_multi_resource_metrics_t metrics;

        bool ok = false;
        if (cfg.pubsub_mode) {
            ok = bench_multi_client_detail::run_pubsub_duration (
              sockets,
              settings,
              poll_items,
              scratch,
              &throughput,
              &latency,
              &metrics);
        } else {
            ok = bench_multi_client_detail::run_echo_duration (
              sockets,
              cfg,
              settings,
              payload,
              payload_size,
              server_id,
              pending,
              poll_items,
              scratch,
              &throughput,
              &latency,
              &metrics);
        }

        if (!ok)
        {
            if (bench_debug_enabled ()) {
                std::cerr << cfg.pattern
                          << ": benchmark phase failed for msg_size="
                          << msg_size << std::endl;
            }
            return 1;
        }

        bench_multi_client_detail::print_client_result_lines (
          lib_name,
          cfg,
          transport,
          msg_size,
          throughput,
          latency,
          metrics);
    }

    bench_multi_client_detail::close_client_sockets (&sockets);

    return 0;
}

#endif
