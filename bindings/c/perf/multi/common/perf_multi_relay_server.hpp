#ifndef PERF_MULTI_RELAY_SERVER_HPP
#define PERF_MULTI_RELAY_SERVER_HPP

#include "perf_common.hpp"
#include "perf_common_multi.hpp"
#include "perf_multi_client_helpers.hpp"
#include "../../common/perf_c_agg.hpp"
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

inline bool is_transient_reply_err (int err)
{
    return err == EAGAIN || err == EINTR || err == EHOSTUNREACH
           || err == ENOTCONN;
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

inline bool clone_multipart (zlink_msg_t *parts,
                             size_t part_count,
                             std::vector<zlink_msg_t> *out)
{
    if (!out)
        return false;

    out->clear ();
    if (!parts || part_count == 0)
        return true;

    out->resize (part_count);
    for (size_t i = 0; i < part_count; ++i)
        zlink_msg_init (&(*out)[i]);

    for (size_t i = 0; i < part_count; ++i) {
        if (zlink_msg_copy (&(*out)[i], &parts[i]) == 0)
            continue;

        for (size_t j = 0; j < part_count; ++j)
            zlink_msg_close (&(*out)[j]);
        out->clear ();
        return false;
    }

    return true;
}

inline bool relay_router_once (void *server)
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

    std::vector<zlink_msg_t> reply_parts;
    if (!clone_multipart (parts, part_count, &reply_parts)) {
        zlink_multipart_close (parts, part_count);
        return false;
    }
    zlink_multipart_close (parts, part_count);

    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (100);
    for (;;) {
        const zlink_submit_result_t send_rc =
          ::zlink_send_rid (
            server,
            source_rid,
            reply_parts.data (),
            reply_parts.size (),
            static_cast<zlink_send_flags_t> (0));
        if (send_rc == ZLINK_SUBMIT_OK)
            return true;

        const int err = zlink_errno ();
        if (!is_transient_reply_err (err)
            || std::chrono::steady_clock::now () >= deadline) {
            if (bench_debug_enabled ()) {
                std::cerr << "[perf-multi-relay] reply send failed err=" << err
                          << std::endl;
            }
            for (size_t i = 0; i < reply_parts.size (); ++i)
                zlink_msg_close (&reply_parts[i]);
            return false;
        }
        (void) perf_socket_poll (NULL, 0, 1);
    }
}

inline void print_server_metrics (
  const relay_server_config_t &config,
  const std::string &lib_name,
  const std::string &transport,
  const std::vector<size_t> &sizes)
{
    (void) config;
    (void) lib_name;
    (void) transport;
    (void) sizes;
}

inline bool run_server_loop (const relay_server_config_t &config,
                             void *server,
                             const std::string &lib_name,
                             const std::string &transport)
{
    if (!server)
        return false;

    while (!perf_stop_requested ().load (std::memory_order_acquire)) {
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

    std::vector<size_t> sizes = resolve_bench_msg_sizes (64);
    if (sizes.empty ())
        sizes.push_back (64);

    std::cout << "READY," << endpoint << std::endl;

    const bool loop_ok = run_server_loop (config, server, lib_name, transport);
    print_server_metrics (config, lib_name, transport, sizes);

    zlink_close (server);
    return loop_ok ? 0 : 1;
}

} // namespace perf_multi_relay_server

#endif
