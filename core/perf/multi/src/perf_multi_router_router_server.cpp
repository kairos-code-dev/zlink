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
static const zlink_socket_type_t k_server_socket_type = ZLINK_ROUTER;
static const bool k_server_has_routing_id = true;
static const char *k_server_routing_id = "SERVER";

static std::atomic<bool> g_stop_requested (false);
static std::atomic<bool> g_queue_probe_pending (false);
static std::atomic<size_t> g_queue_probe_size (0);

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

inline void request_queue_probe (size_t msg_size)
{
    if (msg_size == 0)
        return;
    g_queue_probe_size.store (msg_size, std::memory_order_release);
    g_queue_probe_pending.store (true, std::memory_order_release);
}

inline void emit_requested_queue_probe (const std::string &lib_name,
                                        const std::string &transport,
                                        void *send_socket,
                                        void *recv_socket)
{
    if (!g_queue_probe_pending.exchange (false, std::memory_order_acq_rel))
        return;

    const size_t msg_size = g_queue_probe_size.load (std::memory_order_acquire);
    if (msg_size == 0 || !send_socket || !recv_socket)
        return;

    const server_queue_stats_t queue_stats =
      sample_server_queue_stats (send_socket, recv_socket);
    print_server_queue_metrics (
      lib_name, k_pattern, transport, msg_size, queue_stats);
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

enum relay_status_t
{
    relay_error = -1,
    relay_idle = 0,
    relay_progress = 1
};

inline relay_status_t relay_router_message_non_blocking (void *server)
{
    // Identity frame
    zlink_msg_t identity;
    if (zlink_msg_init (&identity) != 0)
        return relay_error;

    int recv_rc = zlink_msg_recv (&identity, server, ZLINK_DONTWAIT);
    while (recv_rc < 0 && zlink_errno () == EINTR)
        recv_rc = zlink_msg_recv (&identity, server, ZLINK_DONTWAIT);
    if (recv_rc < 0) {
        const int err = zlink_errno ();
        zlink_msg_close (&identity);
        if (err == EAGAIN)
            return relay_idle;
        return relay_error;
    }

    while (!g_stop_requested.load (std::memory_order_acquire)) {
        const int send_rc = zlink_msg_send (&identity, server, ZLINK_SNDMORE);
        if (send_rc >= 0)
            break;
        const int err = zlink_errno ();
        if (err == EINTR)
            continue;
        if (err == EAGAIN) {
            std::this_thread::yield ();
            continue;
        }
        zlink_msg_close (&identity);
        return relay_error;
    }
    zlink_msg_close (&identity);

    // Payload frame
    zlink_msg_t payload;
    if (zlink_msg_init (&payload) != 0)
        return relay_error;

    recv_rc = zlink_msg_recv (&payload, server, 0);
    while (recv_rc < 0 && zlink_errno () == EINTR)
        recv_rc = zlink_msg_recv (&payload, server, 0);
    if (recv_rc < 0) {
        zlink_msg_close (&payload);
        return relay_error;
    }

    while (!g_stop_requested.load (std::memory_order_acquire)) {
        const int send_rc = zlink_msg_send (&payload, server, 0);
        if (send_rc >= 0)
            break;
        const int err = zlink_errno ();
        if (err == EINTR)
            continue;
        if (err == EAGAIN) {
            std::this_thread::yield ();
            continue;
        }
        zlink_msg_close (&payload);
        return relay_error;
    }
    zlink_msg_close (&payload);

    return relay_progress;
}

inline bool relay_router_once (void *server, int poll_timeout_ms)
{
    zlink_pollitem_t item[] = {{server, 0, ZLINK_POLLIN, 0}};
    const int prc = zlink_poll (item, 1, poll_timeout_ms);
    if (prc < 0)
        return zlink_errno () == EINTR;
    if (prc == 0 || (item[0].revents & ZLINK_POLLIN) == 0)
        return true;

    while (!g_stop_requested.load (std::memory_order_acquire)) {
        const relay_status_t status = relay_router_message_non_blocking (server);
        if (status == relay_error)
            return false;
        if (status == relay_idle)
            break;
    }

    return true;
}

inline void print_server_metrics (
  const std::string &lib_name,
  const std::string &transport,
  const std::vector<size_t> &sizes,
  const bench_multi_resource_metrics_t &metrics,
  const server_queue_stats_t &queue_stats)
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
        print_server_queue_metrics (
          lib_name,
          k_pattern,
          transport,
          sizes[i],
          queue_stats);
    }
}

inline bool run_server_loop (void *server,
                             const multi_bench_settings_t &settings,
                             const std::string &lib_name,
                             const std::string &transport)
{
    (void) settings;
    if (!server)
        return false;

    while (!g_stop_requested.load (std::memory_order_acquire)) {
        emit_requested_queue_probe (lib_name, transport, server, server);
        if (!relay_router_once (server, 50)) {
            return false;
        }
    }

    return true;
}

inline int run_server_benchmark (const std::string &lib_name,
                                 const std::string &transport)
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
    g_queue_probe_pending.store (false, std::memory_order_release);
    g_queue_probe_size.store (0, std::memory_order_release);
    install_signal_handlers ();

    std::thread stdin_watcher ([] () {
        std::string line;
        while (std::getline (std::cin, line)) {
            size_t queue_size = 0;
            if (parse_queue_probe_command (line, &queue_size)) {
                request_queue_probe (queue_size);
                continue;
            }
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

    const bool loop_ok = run_server_loop (server, settings, lib_name, transport);

    const bench_multi_resource_metrics_t metrics =
      bench_multi_finish_resource_probe (sample_start);
    const server_queue_stats_t queue_stats =
      sample_server_queue_stats (server, server);
    print_server_metrics (lib_name, transport, sizes, metrics, queue_stats);

    close_connect_monitor (server_monitor);
    zlink_close (server);

    return loop_ok ? 0 : 1;
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
