#ifndef PERF_MULTI_CLIENT_HELPERS_HPP
#define PERF_MULTI_CLIENT_HELPERS_HPP

#include "perf_common.hpp"
#include "perf_common_multi.hpp"

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
    send_would_block = 1,
    send_error = 2
};

struct pubsub_worker_stats_t
{
    long recv_count;
    double lat_sum;
    long lat_count;
    bench_latency_sampler_t lat_samples;
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
                                        bool non_blocking,
                                        bool router_send)
{
    const int base_flags = non_blocking ? ZLINK_DONTWAIT : 0;

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

inline int recv_one_message (void *socket, std::vector<char> &scratch, int flags)
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

    const int linger_ms = 0;
    for (size_t i = 0; i < sockets_out->size (); ++i) {
        void *sock = zlink_socket (ctx.get (), client_socket_type);
        if (!sock)
            return false;

        set_sockopt_int (sock, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
        apply_benchmark_hwm (sock, settings.hwm);

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
                                       long *lat_count,
                                       bench_latency_sampler_t *lat_samples)
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

} // namespace perf_multi_client

#endif
