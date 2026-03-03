#ifndef BENCH_MULTI_SERVER_HPP
#define BENCH_MULTI_SERVER_HPP

#include "bench_multi_pattern.hpp"
#include "bench_multi_resource.hpp"
#include "bench_common_multi.hpp"

#include "../../../../perf/multi/common/perf_common.hpp"

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace bench_multi_server_detail {

static std::atomic<bool> g_stop_requested (false);

struct bench_multi_relay_stats_t
{
    long dealer_recv;
    long dealer_send_ok;
    long dealer_send_eagain;
    long dealer_send_eintr;
    long dealer_send_error;
    bench_multi_relay_stats_t ()
      : dealer_recv (0),
        dealer_send_ok (0),
        dealer_send_eagain (0),
        dealer_send_eintr (0),
        dealer_send_error (0)
    {
    }
};

inline void signal_handler (int)
{
    g_stop_requested.store (true, std::memory_order_release);
}

inline void install_signal_handlers ()
{
    std::signal (SIGINT, signal_handler);
#if defined(SIGTERM)
    std::signal (SIGTERM, signal_handler);
#endif
}

inline std::vector<size_t> resolve_server_sizes ()
{
    std::vector<size_t> sizes = resolve_bench_msg_sizes (64);
    if (sizes.empty ())
        sizes.push_back (64);
    return sizes;
}

inline size_t resolve_max_size (const std::vector<size_t> &sizes)
{
    size_t max_size = 64;
    for (size_t i = 0; i < sizes.size (); ++i) {
        if (sizes[i] > max_size)
            max_size = sizes[i];
    }
    return max_size;
}

inline unsigned long long wallclock_now_us ()
{
    return static_cast<unsigned long long> (
      std::chrono::duration_cast<std::chrono::microseconds> (
        std::chrono::system_clock::now ().time_since_epoch ())
        .count ());
}

inline bool endpoint_needs_loopback_patch (const std::string &transport)
{
    return transport == "tcp" || transport == "ws" || transport == "tls"
           || transport == "wss";
}

inline void patch_endpoint_to_loopback (const std::string &transport,
                                        std::string *endpoint)
{
    if (!endpoint || endpoint->empty ()
        || !endpoint_needs_loopback_patch (transport))
        return;

    const std::string any_ipv4 = "://0.0.0.0:";
    const std::string any_ipv6 = "://[::]:";
    std::size_t pos = endpoint->find (any_ipv4);
    if (pos != std::string::npos) {
        endpoint->replace (pos, any_ipv4.size (), "://127.0.0.1:");
        return;
    }

    pos = endpoint->find (any_ipv6);
    if (pos != std::string::npos)
        endpoint->replace (pos, any_ipv6.size (), "://127.0.0.1:");
}

inline std::string bind_server_endpoint (void *socket,
                                         const std::string &transport,
                                         const std::string &token)
{
    const int bind_port =
      resolve_multi_int_env ("BENCH_MULTI_SERVER_BIND_PORT", 0, 0);
    if (bind_port <= 0)
        return bind_and_resolve_endpoint (
          socket,
          transport,
          std::string ("zlink_multi_") + token + "_server");

    const std::string endpoint = make_fixed_endpoint (transport, bind_port);
    if (zlink_bind (socket, endpoint.c_str ()) != 0) {
        std::cerr << "bind failed for " << endpoint << ": "
                  << zlink_strerror (zlink_errno ()) << std::endl;
        return std::string ();
    }

    std::string resolved = endpoint;
    char last_endpoint[MAX_SOCKET_STRING] = "";
    size_t size = sizeof (last_endpoint);
    if (zlink_getsockopt (socket, ZLINK_LAST_ENDPOINT, last_endpoint, &size)
        == 0) {
        resolved.assign (last_endpoint);
    }
    patch_endpoint_to_loopback (transport, &resolved);
    apply_debug_timeouts (socket, transport);
    return resolved;
}

inline bool relay_router_once (void *server,
                               std::vector<char> &id_buf,
                               std::vector<char> &payload_buf,
                               int poll_timeout_ms)
{
    zlink_pollitem_t item[] = {{server, 0, ZLINK_POLLIN, 0}};
    const int prc = zlink_poll (item, 1, poll_timeout_ms);
    if (prc < 0)
        return zlink_errno () == EINTR;
    if (prc == 0 || (item[0].revents & ZLINK_POLLIN) == 0)
        return true;

    const int id_len = zlink_recv (server, id_buf.data (), id_buf.size (), 0);
    if (id_len < 0)
        return zlink_errno () == EINTR;

    int payload_len = 0;
    bool has_payload = false;
    while (true) {
        int more = 0;
        size_t more_size = sizeof (more);
        if (zlink_getsockopt (server, ZLINK_RCVMORE, &more, &more_size) != 0)
            break;
        if (!more)
            break;

        const int len =
          zlink_recv (server, payload_buf.data (), payload_buf.size (), 0);
        if (len < 0) {
            if (zlink_errno () == EINTR)
                continue;
            return false;
        }
        payload_len = len;
        has_payload = true;
    }

    if (zlink_send (
          server,
          id_buf.data (),
          static_cast<size_t> (id_len),
          ZLINK_SNDMORE)
        < 0)
        return zlink_errno () == EINTR || zlink_errno () == EAGAIN;

    if (!has_payload) {
        if (zlink_send (server, "", 0, 0) < 0)
            return zlink_errno () == EINTR || zlink_errno () == EAGAIN;
        return true;
    }

    if (zlink_send (
          server,
          payload_buf.data (),
          static_cast<size_t> (payload_len),
          0)
        < 0)
        return zlink_errno () == EINTR || zlink_errno () == EAGAIN;

    return true;
}

inline bool relay_dealer_once (void *server,
                               std::vector<char> &payload,
                               int poll_timeout_ms,
                               bench_multi_relay_stats_t *stats = NULL)
{
    zlink_pollitem_t item[] = {{server, 0, ZLINK_POLLIN, 0}};
    const int prc = zlink_poll (item, 1, poll_timeout_ms);
    if (prc < 0)
        return zlink_errno () == EINTR;
    if (prc == 0 || (item[0].revents & ZLINK_POLLIN) == 0)
        return true;

    const int len = zlink_recv (server, payload.data (), payload.size (), 0);
    if (len < 0)
        return zlink_errno () == EINTR;
    if (stats)
        ++stats->dealer_recv;

    if (zlink_send (
          server,
          payload.data (),
          static_cast<size_t> (len),
          0)
        < 0) {
        const int err = zlink_errno ();
        if (stats) {
            if (err == EAGAIN)
                ++stats->dealer_send_eagain;
            else if (err == EINTR)
                ++stats->dealer_send_eintr;
            else
                ++stats->dealer_send_error;
        }
        return err == EINTR || err == EAGAIN;
    }
    if (stats)
        ++stats->dealer_send_ok;

    return true;
}

inline bool publish_once (void *server,
                          std::vector<char> &payload,
                          int send_backoff_us)
{
    if (payload.size () >= sizeof (unsigned long long)) {
        const unsigned long long now_us = wallclock_now_us ();
        std::memcpy (payload.data (), &now_us, sizeof (now_us));
    }

    if (zlink_send (server, payload.data (), payload.size (), ZLINK_DONTWAIT)
        >= 0) {
        return true;
    }

    const int err = zlink_errno ();
    if (err == EAGAIN || err == EINTR) {
        if (send_backoff_us > 0) {
            std::this_thread::sleep_for (
              std::chrono::microseconds (send_backoff_us));
        }
        return true;
    }

    return false;
}

inline void print_server_metrics (
  const std::string &lib_name,
  const multi_pattern_config_t &cfg,
  const std::string &transport,
  const std::vector<size_t> &sizes,
  const bench_multi_resource_metrics_t &metrics)
{
    for (size_t i = 0; i < sizes.size (); ++i) {
        if (metrics.has_cpu_pct) {
            std::cout << "RESULT," << lib_name << "," << cfg.pattern << ","
                      << transport << "," << sizes[i]
                      << ",server_cpu_pct," << std::fixed
                      << std::setprecision (2) << metrics.cpu_pct << std::endl;
        }
        if (metrics.has_mem_mb) {
            std::cout << "RESULT," << lib_name << "," << cfg.pattern << ","
                      << transport << "," << sizes[i]
                      << ",server_mem_mb," << std::fixed
                      << std::setprecision (2) << metrics.mem_mb << std::endl;
        }
    }
}

} // namespace bench_multi_server_detail

inline int run_multi_server_main (int argc,
                                  char **argv,
                                  const multi_pattern_config_t &cfg)
{
    if (argc < 3)
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];

    if (!transport_available (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << cfg.pattern << ","
                  << transport << std::endl;
        return 0;
    }

    ctx_guard_t ctx;
    if (!ctx.valid ())
        return 1;

    void *server = zlink_socket (ctx.get (), cfg.server_socket_type);
    if (!server)
        return 1;

    const multi_bench_settings_t settings = resolve_multi_bench_settings ();
    const int linger_ms = 0;
    set_sockopt_int (server, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
    apply_benchmark_hwm (server, settings.hwm);
    if (cfg.server_has_routing_id && cfg.server_routing_id) {
        zlink_setsockopt (
          server,
          ZLINK_ROUTING_ID,
          cfg.server_routing_id,
          std::strlen (cfg.server_routing_id));
    }

    if (!setup_tls_server (server, transport)) {
        zlink_close (server);
        return 1;
    }

    connect_monitor_t server_monitor;
    if (!open_connect_monitor (server, server_monitor)) {
        zlink_close (server);
        return 1;
    }

    const std::string endpoint = bench_multi_server_detail::bind_server_endpoint (
      server, transport, cfg.token);
    if (endpoint.empty ()) {
        close_connect_monitor (server_monitor);
        zlink_close (server);
        return 1;
    }

    bench_multi_server_detail::g_stop_requested.store (
      false, std::memory_order_release);
    bench_multi_server_detail::install_signal_handlers ();

    std::thread stdin_watcher ([] () {
        std::string line;
        while (std::getline (std::cin, line)) {
            if (line == "STOP" || line == "QUIT") {
                bench_multi_server_detail::g_stop_requested.store (
                  true, std::memory_order_release);
                return;
            }
        }
        bench_multi_server_detail::g_stop_requested.store (
          true, std::memory_order_release);
    });
    stdin_watcher.detach ();

    const std::vector<size_t> sizes = bench_multi_server_detail::resolve_server_sizes ();
    const size_t max_size = bench_multi_server_detail::resolve_max_size (sizes);
    std::vector<char> payload (
      std::max<size_t> (max_size, sizeof (unsigned long long)),
      's');
    std::vector<char> router_id_buf (
      std::max<size_t> (256, static_cast<size_t> (1024)), '\0');
    std::vector<char> router_payload_buf (
      std::max<size_t> (max_size + 256, static_cast<size_t> (1024)), '\0');
    bench_multi_server_detail::bench_multi_relay_stats_t relay_stats;

    const bench_multi_cpu_sample_t sample_start = bench_multi_capture_cpu_sample ();

    std::cout << "READY," << endpoint << std::endl;

    const auto wait_started = std::chrono::steady_clock::now ();
    if (bench_debug_enabled ()) {
        std::cerr << cfg.pattern << ": wait_connect_ready_count start"
                  << " expected=" << settings.clients
                  << " timeout_ms=" << settings.connect_ready_timeout_ms
                  << std::endl;
    }
    if (!wait_connect_ready_count (
          server_monitor,
          settings.clients,
          settings.connect_ready_timeout_ms)) {
        std::cerr << cfg.pattern << ": connect_ready wait timeout (expected="
                  << settings.clients << ")" << std::endl;
        close_connect_monitor (server_monitor);
        zlink_close (server);
        return 1;
    }
    if (bench_debug_enabled ()) {
        const auto wait_elapsed_ms =
          std::chrono::duration_cast<std::chrono::milliseconds> (
            std::chrono::steady_clock::now () - wait_started)
            .count ();
        std::cerr << cfg.pattern << ": wait_connect_ready_count done"
                  << " elapsed_ms=" << wait_elapsed_ms << std::endl;
    }

    bool loop_ok = true;
    while (!bench_multi_server_detail::g_stop_requested.load (
      std::memory_order_acquire)) {
        if (cfg.pubsub_mode) {
            if (!bench_multi_server_detail::publish_once (
                  server,
                  payload,
                  settings.send_backoff_us)) {
                loop_ok = false;
                break;
            }
            continue;
        }

        if (cfg.server_router_echo) {
            if (!bench_multi_server_detail::relay_router_once (
                  server, router_id_buf, router_payload_buf, 50)) {
                loop_ok = false;
                break;
            }
            continue;
        }

        if (!bench_multi_server_detail::relay_dealer_once (
              server,
              payload,
              50,
              &relay_stats)) {
            loop_ok = false;
            break;
        }
    }

    if (bench_debug_enabled ()) {
        std::cerr << cfg.pattern
                  << ": dealer relay summary recv=" << relay_stats.dealer_recv
                  << " send_ok=" << relay_stats.dealer_send_ok
                  << " send_eagain=" << relay_stats.dealer_send_eagain
                  << " send_eintr=" << relay_stats.dealer_send_eintr
                  << " send_error=" << relay_stats.dealer_send_error
                  << std::endl;
    }

    const bench_multi_resource_metrics_t metrics =
      bench_multi_finish_resource_probe (sample_start);
    bench_multi_server_detail::print_server_metrics (
      lib_name, cfg, transport, sizes, metrics);

    close_connect_monitor (server_monitor);
    zlink_close (server);

    return loop_ok ? 0 : 1;
}

#endif
