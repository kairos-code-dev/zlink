#include "../common/perf_multi_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"

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

static const char *k_pattern = "MULTI_ROUTER_ROUTER";
static const char *k_token = "router_router";
static const int k_server_socket_type = ZLINK_ROUTER;
static const bool k_server_router_echo = true;
static const bool k_pubsub_mode = false;
static const bool k_server_has_routing_id = true;
static const char *k_server_routing_id = "SERVER";

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
                    endpoint_any.replace (pos, any_v6.size (), "://127.0.0.1:");
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
        < 0) {
        const int err = zlink_errno ();
        return err == EINTR || err == EAGAIN;
    }

    if (!has_payload) {
        if (zlink_send (server, "", 0, 0) < 0) {
            const int err = zlink_errno ();
            return err == EINTR || err == EAGAIN;
        }
        return true;
    }

    if (zlink_send (
          server,
          payload_buf.data (),
          static_cast<size_t> (payload_len),
          0)
        < 0) {
        const int err = zlink_errno ();
        return err == EINTR || err == EAGAIN;
    }

    return true;
}

inline bool relay_dealer_once (void *server,
                               std::vector<char> &payload,
                               int poll_timeout_ms)
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

    if (zlink_send (
          server,
          payload.data (),
          static_cast<size_t> (len),
          0)
        < 0) {
        const int err = zlink_errno ();
        return err == EINTR || err == EAGAIN;
    }

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

inline size_t resolve_max_size (const std::vector<size_t> &sizes)
{
    size_t max_size = 64;
    for (size_t i = 0; i < sizes.size (); ++i) {
        if (sizes[i] > max_size)
            max_size = sizes[i];
    }
    return max_size;
}

inline void print_server_metrics (
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

} // namespace

int main (int argc, char **argv)
{
    if (argc < 3)
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
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
    const int linger_ms = 0;
    set_sockopt_int (server, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
    apply_benchmark_hwm (server, settings.hwm);
    if (k_server_has_routing_id) {
        zlink_setsockopt (
          server,
          ZLINK_ROUTING_ID,
          k_server_routing_id,
          std::strlen (k_server_routing_id));
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

    const size_t max_size = resolve_max_size (sizes);
    std::vector<char> payload (
      std::max<size_t> (
        static_cast<size_t> (1024),
        std::max<size_t> (max_size, sizeof (unsigned long long))),
      's');
    std::vector<char> router_id_buf (
      std::max<size_t> (256, static_cast<size_t> (1024)),
      '\0');
    std::vector<char> router_payload_buf (
      std::max<size_t> (max_size + 256, static_cast<size_t> (1024)),
      '\0');

    const bench_multi_cpu_sample_t sample_start = bench_multi_capture_cpu_sample ();

    std::cout << "READY," << endpoint << std::endl;

    if (!wait_connect_ready_count (
          server_monitor,
          settings.clients,
          settings.connect_ready_timeout_ms)) {
        close_connect_monitor (server_monitor);
        zlink_close (server);
        return 1;
    }

    bool loop_ok = true;
    while (!g_stop_requested.load (std::memory_order_acquire)) {
        if (k_pubsub_mode) {
            if (!publish_once (server, payload, settings.send_backoff_us)) {
                loop_ok = false;
                break;
            }
            continue;
        }

        if (k_server_router_echo) {
            if (!relay_router_once (
                  server,
                  router_id_buf,
                  router_payload_buf,
                  50)) {
                loop_ok = false;
                break;
            }
            continue;
        }

        if (!relay_dealer_once (server, payload, 50)) {
            loop_ok = false;
            break;
        }
    }

    const bench_multi_resource_metrics_t metrics =
      bench_multi_finish_resource_probe (sample_start);
    print_server_metrics (lib_name, transport, sizes, metrics);

    close_connect_monitor (server_monitor);
    zlink_close (server);

    return loop_ok ? 0 : 1;
}
