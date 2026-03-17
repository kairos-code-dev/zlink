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

static const char *k_pattern = "MULTI_DEALER_ROUTER";
static const char *k_token = "dealer_router";
static const zlink_socket_type_t k_server_socket_type = ZLINK_ROUTER;
static const bool k_server_has_routing_id = false;
static const char *k_server_routing_id = "SERVER";

static std::atomic<bool> g_queue_probe_pending (false);
static std::atomic<size_t> g_queue_probe_size (0);

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

inline bool relay_router_once (void *server,
                               std::vector<char> &id_buf,
                               std::vector<char> &payload_buf,
                               int poll_timeout_ms)
{
    const int id_len =
      zlink_recv (server, id_buf.data (), id_buf.size (), ZLINK_DONTWAIT);
    if (id_len < 0) {
        const int err = zlink_errno ();
        if (err == EAGAIN) {
            (void) poll_timeout_ms;
            std::this_thread::yield ();
            return true;
        }
        return err == EINTR;
    }

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
        return zlink_errno () == EINTR || zlink_errno () == EAGAIN;
    }

    if (!has_payload)
        return zlink_send (server, "", 0, 0) >= 0
               || zlink_errno () == EINTR || zlink_errno () == EAGAIN;

    if (zlink_send (
          server,
          payload_buf.data (),
          static_cast<size_t> (payload_len),
          0)
        < 0) {
        return zlink_errno () == EINTR || zlink_errno () == EAGAIN;
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
                             const std::string &transport,
                             std::vector<char> &router_id_buf,
                             std::vector<char> &router_payload_buf)
{
    (void) settings;
    if (!server)
        return false;

    while (!perf_stop_requested ().load (std::memory_order_acquire)) {
        emit_requested_queue_probe (lib_name, transport, server, server);
        if (!relay_router_once (
              server, router_id_buf, router_payload_buf, 50)) {
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

    perf_stop_requested ().store (false, std::memory_order_release);
    g_queue_probe_pending.store (false, std::memory_order_release);
    g_queue_probe_size.store (0, std::memory_order_release);
    install_perf_signal_handlers ();

    std::thread stdin_watcher ([] () {
        std::string line;
        while (std::getline (std::cin, line)) {
            size_t queue_size = 0;
            if (parse_queue_probe_command (line, &queue_size)) {
                request_queue_probe (queue_size);
                continue;
            }
            if (line == "STOP" || line == "QUIT") {
                perf_stop_requested ().store (true, std::memory_order_release);
                return;
            }
        }
        perf_stop_requested ().store (true, std::memory_order_release);
    });
    stdin_watcher.detach ();

    std::vector<size_t> sizes = resolve_bench_msg_sizes (64);
    if (sizes.empty ())
        sizes.push_back (64);
    size_t max_msg_size = 64;
    for (size_t i = 0; i < sizes.size (); ++i) {
        if (sizes[i] > max_msg_size)
            max_msg_size = sizes[i];
    }

    const bench_multi_cpu_sample_t sample_start = bench_multi_capture_cpu_sample ();

    std::cout << "READY," << endpoint << std::endl;

    std::vector<char> router_id_buf (
      std::max<size_t> (256, static_cast<size_t> (1024)), '\0');
    std::vector<char> router_payload_buf (
      std::max<size_t> (
        max_msg_size + 256,
        static_cast<size_t> (1024)),
      '\0');

    if (bench_debug_enabled ()) {
        std::cerr << "[dealer-router-server] skipping connect barrier ready="
                  << poll_connect_ready_count (server_monitor) << std::endl;
    }

    const bool loop_ok = run_server_loop (
      server,
      settings,
      lib_name,
      transport,
      router_id_buf,
      router_payload_buf);

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
