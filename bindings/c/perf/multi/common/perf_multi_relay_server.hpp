#ifndef PERF_MULTI_RELAY_SERVER_HPP
#define PERF_MULTI_RELAY_SERVER_HPP

#include "perf_common.hpp"
#include "perf_common_multi.hpp"
#include "perf_multi_client_helpers.hpp"
#include "../../common/perf_tls_setup.hpp"
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

static std::atomic<int> g_debug_relay_logs (0);

inline std::string format_rid_debug (const zlink_routing_id_t *rid)
{
    if (!rid || rid->size == 0)
        return "<empty>";

    std::ostringstream os;
    for (size_t i = 0; i < rid->size; ++i) {
        const unsigned char c = rid->data[i];
        if (i != 0)
            os << ' ';
        if (c >= 32 && c <= 126)
            os << static_cast<char> (c);
        else
            os << '.';
        os << std::hex << std::uppercase << std::setw (2) << std::setfill ('0')
           << static_cast<unsigned> (c) << std::dec;
    }
    return os.str ();
}

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

inline bool relay_router_once (void *server,
                               zlink_socket_type_t socket_type,
                               int hwm_value,
                               const std::string &transport,
                               size_t *active_msg_size)
{
    const zlink_routing_id_t *source_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    const zlink_recv_result_t rc =
      ::zlink_router_recv (
        server,
        &source_rid,
        &source_spot_rid,
        &request_seq,
        &parts,
        &part_count,
        static_cast<zlink_recv_flags_t> (0));
    if (rc != ZLINK_RECV_OK) {
        const int err = zlink_errno ();
        return err == EAGAIN || err == EINTR;
    }

    if (!source_rid || source_rid->size == 0
        || (source_spot_rid && source_spot_rid->size != 0)
        || request_seq != 0 || part_count == 0 || !parts) {
        zlink_multipart_close (parts, part_count);
        errno = EPROTO;
        return false;
    }
    if (bench_debug_enabled ()
        && g_debug_relay_logs.fetch_add (1, std::memory_order_acq_rel) < 12) {
        std::cerr << "[perf-multi-relay] echo request size="
                  << (part_count > 0 && parts ? zlink_msg_size (&parts[0]) : 0)
                  << " rid_size=" << static_cast<int> (source_rid->size)
                  << " rid=" << format_rid_debug (source_rid)
                  << " part_count=" << part_count
                  << std::endl;
    }

    const size_t msg_size =
      part_count > 0 && parts ? zlink_msg_size (&parts[0]) : 0;
    if (active_msg_size && msg_size > 0 && *active_msg_size != msg_size) {
        apply_benchmark_auto_hwm_msg_unit (server, socket_type, msg_size);
        apply_benchmark_hwm (server, hwm_value);
        *active_msg_size = msg_size;
        perf_print_auto_hwm_snapshot (
          server, false, "server", transport, true, msg_size, socket_type);
    }

    const zlink_submit_result_t send_rc =
      ::zlink_send_rid (
        server, source_rid, parts, part_count,
        static_cast<zlink_send_flags_t> (0));
    if (send_rc != ZLINK_SUBMIT_OK) {
        if (bench_debug_enabled ()) {
            std::cerr << "[perf-multi-relay] reply send failed err="
                      << zlink_errno () << std::endl;
        }
        return false;
    }
    return true;
}

inline bool run_server_loop (const relay_server_config_t &config,
                             void *server,
                             int hwm_value,
                             const std::string &lib_name,
                             const std::string &transport)
{
    if (!server)
        return false;

    size_t active_msg_size = 0;
    while (!perf_stop_requested ().load (std::memory_order_acquire)) {
        if (!relay_router_once (
              server,
              config.socket_type,
              hwm_value,
              transport,
              &active_msg_size))
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
    install_perf_signal_handlers ();

    std::thread stdin_watcher ([] () {
        std::string line;
        while (std::getline (std::cin, line)) {
            if (line == "STOP" || line == "QUIT") {
                perf_stop_requested ().store (true, std::memory_order_release);
                return;
            }
        }
        perf_stop_requested ().store (true, std::memory_order_release);
    });
    stdin_watcher.detach ();

    std::cout << "READY," << endpoint << std::endl;

    const bool loop_ok =
      run_server_loop (config, server, settings.hwm, lib_name, transport);

    zlink_close (server);
    return loop_ok ? 0 : 1;
}

} // namespace perf_multi_relay_server

#endif
