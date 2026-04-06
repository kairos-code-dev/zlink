#ifndef PERF_MULTI_RELAY_SERVER_HPP
#define PERF_MULTI_RELAY_SERVER_HPP

#include "perf_common.hpp"
#include "perf_common_multi.hpp"
#include "perf_multi_client_helpers.hpp"
#include "../../common/perf_tls_setup.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace perf_multi_relay_server {

using ::setup_tls_server;

struct relay_server_config_t
{
    relay_server_config_t () :
        pattern_name (NULL),
        token (NULL),
        socket_type (ZLINK_SOCKET_ROUTER),
        has_server_routing_id (false),
        server_routing_id (NULL)
    {
    }

    const char *pattern_name;
    const char *token;
    zlink_socket_type_t socket_type;
    bool has_server_routing_id;
    const char *server_routing_id;
};

inline std::atomic<bool> &queue_probe_pending ()
{
    static std::atomic<bool> value (false);
    return value;
}

inline std::atomic<size_t> &queue_probe_size ()
{
    static std::atomic<size_t> value (0);
    return value;
}

inline void request_queue_probe (size_t msg_size)
{
    if (msg_size == 0)
        return;

    queue_probe_size ().store (msg_size, std::memory_order_release);
    queue_probe_pending ().store (true, std::memory_order_release);
}

inline void emit_requested_queue_probe (const relay_server_config_t &config,
                                        const std::string &lib_name,
                                        const std::string &transport,
                                        void *server)
{
    if (!queue_probe_pending ().exchange (false, std::memory_order_acq_rel))
        return;

    const size_t msg_size =
      queue_probe_size ().load (std::memory_order_acquire);
    if (msg_size == 0 || !server)
        return;

    const server_queue_stats_t queue_stats =
      sample_server_queue_stats (server, server);
    print_server_queue_metrics (
      lib_name, config.pattern_name, transport, msg_size, queue_stats);
}

inline bool relay_router_once (void *server)
{
    zlink_routing_id_t source_rid;
    source_rid.size = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    const int rc = ::zlink_recv (server, &source_rid, &parts, &part_count, 0);
    if (rc < 0) {
        const int err = zlink_errno ();
        return err == EAGAIN || err == EINTR;
    }

    if (part_count == 0 || !parts) {
        zlink_msg_t empty_part;
        if (zlink_msg_init_size (&empty_part, 0) != 0)
            return false;

        const int send_rc =
          ::zlink_send_rid (server, &source_rid, &empty_part, 1, 0);
        if (send_rc >= 0)
            return true;

        const int err = zlink_errno ();
        zlink_msg_close (&empty_part);
        return err == EAGAIN || err == EINTR;
    }

    const int send_rc = ::zlink_send_rid (server, &source_rid, parts, part_count, 0);
    if (send_rc >= 0) {
        return true;
    }

    const int err = zlink_errno ();
    zlink_multipart_close (parts, part_count);
    return err == EAGAIN || err == EINTR;
}

inline void print_server_metrics (
  const relay_server_config_t &config,
  const std::string &lib_name,
  const std::string &transport,
  const std::vector<size_t> &sizes,
  const bench_multi_resource_metrics_t &metrics,
  const server_queue_stats_t &queue_stats)
{
    print_server_metrics_for_sizes (
      lib_name, config.pattern_name, transport, sizes, metrics, &queue_stats);
}

inline bool run_server_loop (const relay_server_config_t &config,
                             void *server,
                             const std::string &lib_name,
                             const std::string &transport)
{
    if (!server)
        return false;

    while (!perf_stop_requested ().load (std::memory_order_acquire)) {
        emit_requested_queue_probe (config, lib_name, transport, server);
        if (!relay_router_once (server))
            return false;
    }

    return true;
}

inline int run_server_benchmark (const relay_server_config_t &config,
                                 const std::string &lib_name,
                                 const std::string &transport)
{
    set_perf_multi_pattern_env (config.pattern_name);

    if (!perf_multi_client::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << config.pattern_name
                  << "," << transport << std::endl;
        return 0;
    }

    if (!transport_available (transport)) {
        std::cerr << "transport unavailable: " << transport << std::endl;
        return 1;
    }

    ctx_guard_t ctx;
    if (!ctx.valid ())
        return 1;

    void *server = zlink_socket (ctx.get (), config.socket_type);
    if (!server)
        return 1;

    const multi_bench_settings_t settings = resolve_multi_bench_settings ();
    const int linger_ms = 0;
    set_sockopt_int (server, ZLINK_OPT_LINGER, linger_ms, "ZLINK_OPT_LINGER");
    apply_benchmark_hwm (server, settings.hwm);
    if (config.has_server_routing_id && config.server_routing_id) {
        zlink_set_routing_id (
          server,
          config.server_routing_id,
          std::strlen (config.server_routing_id));
    }

    if (!setup_tls_server (server, transport)) {
        zlink_close (server);
        return 1;
    }

    const std::string endpoint = bind_server_endpoint (
      server,
      transport,
      lib_name + std::string ("_") + config.token + "_server");
    if (endpoint.empty ()) {
        zlink_close (server);
        return 1;
    }

    perf_stop_requested ().store (false, std::memory_order_release);
    queue_probe_pending ().store (false, std::memory_order_release);
    queue_probe_size ().store (0, std::memory_order_release);
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

    const bench_multi_cpu_sample_t sample_start =
      bench_multi_capture_cpu_sample ();

    std::cout << "READY," << endpoint << std::endl;

    const bool loop_ok =
      run_server_loop (config, server, lib_name, transport);

    const bench_multi_resource_metrics_t metrics =
      bench_multi_finish_resource_probe (sample_start);
    const server_queue_stats_t queue_stats =
      sample_server_queue_stats (server, server);
    print_server_metrics (
      config, lib_name, transport, sizes, metrics, queue_stats);

    zlink_close (server);
    return loop_ok ? 0 : 1;
}

} // namespace perf_multi_relay_server

#endif
