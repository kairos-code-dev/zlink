#include "../common/perf_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../../../bench/with_zmq/multi/common/bench_resource.hpp"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifndef ZLINK_STREAM
#define ZLINK_STREAM 11
#endif

#ifndef ZLINK_TCP_NODELAY
#define ZLINK_TCP_NODELAY 26
#endif

namespace {

static const char *k_pattern = "STREAM";

static std::atomic<bool> g_stop_requested (false);
static void *g_server_socket = NULL;
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

inline bool is_stream_event_payload (const unsigned char *data, size_t size)
{
    if (size == 0)
        return true;
    return data && size == 1 && (data[0] == 0x00 || data[0] == 0x01);
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
      resolve_int_env ("PERF_SERVER_BIND_PORT", 0, 0);
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

inline void print_server_metrics (
  const std::string &lib_name,
  const std::string &transport,
  const std::vector<size_t> &sizes,
  const bench_resource_metrics_t &metrics,
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

inline bool send_stream_once (const zlink_routing_id_t *rid,
                              const unsigned char *payload,
                              size_t payload_size)
{
    if (!g_server_socket || !rid || rid->size == 0)
        return false;

    const int retry_limit = std::max (
      1, resolve_int_env ("PERF_STREAM_SEND_RETRIES", 128, 1));
    for (int attempt = 0;
         attempt < retry_limit
         && !g_stop_requested.load (std::memory_order_acquire);
         ++attempt) {
        const int rc =
          zlink_stream_send (g_server_socket, rid, payload, payload_size, 0);
        if (rc == static_cast<int> (payload_size))
            return true;

        if (rc >= 0)
            return false;

        const int err = zlink_errno ();
        if (err == EINTR)
            continue;
        if (err == EAGAIN || err == ETIMEDOUT) {
            std::this_thread::yield ();
            continue;
        }
        if (err == ECONNRESET || err == EHOSTUNREACH || err == ENOTCONN
            || err == EPIPE)
            return true;
        return false;
    }

    // Backpressure can be transient on large ws/wss payloads.
    // Keep the relay loop alive even if this payload could not be echoed in time.
    return true;
}

inline bool extract_stream_routing_id (const zlink_msg_t *id_frame,
                                       zlink_routing_id_t *rid_out)
{
    if (!id_frame || !rid_out)
        return false;

    const size_t id_size = zlink_msg_size (const_cast<zlink_msg_t *> (id_frame));
    if (id_size == 0 || id_size > sizeof (rid_out->data))
        return false;

    const unsigned char *id_data = static_cast<const unsigned char *> (
      zlink_msg_data (const_cast<zlink_msg_t *> (id_frame)));
    if (!id_data)
        return false;

    rid_out->size = static_cast<uint8_t> (id_size);
    std::memcpy (rid_out->data, id_data, id_size);
    return true;
}

enum relay_status_t
{
    relay_idle = 0,
    relay_progress = 1,
    relay_error = 2
};

inline relay_status_t relay_stream_message_non_blocking (void *server)
{
    if (!server)
        return relay_error;

    zlink_msg_t id_frame;
    zlink_msg_t payload_frame;
    zlink_msg_init (&id_frame);
    zlink_msg_init (&payload_frame);

    int id_len = zlink_msg_recv (&id_frame, server, ZLINK_DONTWAIT);
    while (id_len < 0 && zlink_errno () == EINTR)
        id_len = zlink_msg_recv (&id_frame, server, ZLINK_DONTWAIT);
    if (id_len < 0) {
        const int err = zlink_errno ();
        (void) zlink_msg_close (&id_frame);
        (void) zlink_msg_close (&payload_frame);
        if (err == EAGAIN || err == EINTR)
            return relay_idle;
        return relay_error;
    }

    int more = 0;
    size_t more_size = sizeof (more);
    if (zlink_getsockopt (server, ZLINK_RCVMORE, &more, &more_size) != 0 || !more) {
        (void) zlink_msg_close (&id_frame);
        (void) zlink_msg_close (&payload_frame);
        return relay_error;
    }

    zlink_routing_id_t rid;
    const bool rid_ok = extract_stream_routing_id (&id_frame, &rid);
    bool ok = rid_ok;

    // Some transports can surface more than one payload frame for a routing-id.
    // Echo every non-event payload frame instead of dropping extras.
    while (true) {
        int payload_len = zlink_msg_recv (&payload_frame, server, ZLINK_DONTWAIT);
        while (payload_len < 0 && zlink_errno () == EINTR)
            payload_len = zlink_msg_recv (&payload_frame, server, ZLINK_DONTWAIT);
        if (payload_len < 0) {
            const int err = zlink_errno ();
            ok = (err == EAGAIN || err == EINTR);
            break;
        }

        const unsigned char *payload = static_cast<const unsigned char *> (
          zlink_msg_data (&payload_frame));
        const size_t payload_size = zlink_msg_size (&payload_frame);
        if (!is_stream_event_payload (payload, payload_size)) {
            ok = send_stream_once (&rid, payload, payload_size);
        }

        (void) zlink_msg_close (&payload_frame);
        (void) zlink_msg_init (&payload_frame);

        more = 0;
        more_size = sizeof (more);
        if (zlink_getsockopt (server, ZLINK_RCVMORE, &more, &more_size) != 0 || !more)
            break;
    }

    (void) zlink_msg_close (&id_frame);
    (void) zlink_msg_close (&payload_frame);
    return ok ? relay_progress : relay_error;
}

inline bool relay_stream_once (void *server, int poll_timeout_ms)
{
    if (!server)
        return false;

    zlink_pollitem_t item[] = {{server, 0, ZLINK_POLLIN, 0}};
    int prc = zlink_poll (item, 1, poll_timeout_ms);
    if (prc < 0)
        return zlink_errno () == EINTR;
    if (prc == 0 || (item[0].revents & ZLINK_POLLIN) == 0)
        return true;

    // Process at least one message, then drain all currently queued messages
    // without going back to a long poll wait.
    while (!g_stop_requested.load (std::memory_order_acquire)) {
        const relay_status_t status = relay_stream_message_non_blocking (server);
        if (status == relay_error)
            return false;
        if (status == relay_idle)
            break;
    }

    return true;
}

} // namespace

int main (int argc, char **argv)
{
    if (argc < 3)
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    set_perf_pattern_env (k_pattern);

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

    void *server = zlink_socket (ctx.get (), ZLINK_STREAM);
    if (!server)
        return 1;

    const bench_cpu_sample_t cpu_start = bench_capture_cpu_sample ();
    const bench_settings_t settings = resolve_bench_settings ();
    const std::vector<size_t> sizes = resolve_bench_msg_sizes (64);

    const int linger_ms = 0;
    set_sockopt_int (server, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
    apply_benchmark_hwm (server, settings.hwm);
    const int io_timeout_ms = resolve_bench_count ("PERF_STREAM_TIMEOUT_MS", 5000);
    set_sockopt_int (server, ZLINK_SNDTIMEO, io_timeout_ms, "ZLINK_SNDTIMEO");
    set_sockopt_int (server, ZLINK_RCVTIMEO, io_timeout_ms, "ZLINK_RCVTIMEO");
    const int nodelay = 1;
    set_sockopt_int (server, ZLINK_TCP_NODELAY, nodelay, "ZLINK_TCP_NODELAY");

    if (!setup_tls_server (server, transport)) {
        zlink_close (server);
        return 1;
    }

    const std::string endpoint = bind_server_endpoint (
      server, transport, lib_name + "_stream_server");
    if (endpoint.empty ()) {
        zlink_close (server);
        return 1;
    }

    g_stop_requested.store (false, std::memory_order_release);
    g_queue_probe_pending.store (false, std::memory_order_release);
    g_queue_probe_size.store (0, std::memory_order_release);
    g_server_socket = server;
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

    std::cout << "READY," << endpoint << std::endl;

    int rc = 0;
    while (!g_stop_requested.load (std::memory_order_acquire)) {
        emit_requested_queue_probe (lib_name, transport, server, server);
        if (!relay_stream_once (server, 50)) {
            rc = 1;
            break;
        }
    }

    g_server_socket = NULL;

    const bench_resource_metrics_t metrics =
      bench_finish_resource_probe (cpu_start);
    const server_queue_stats_t queue_stats =
      sample_server_queue_stats (server, server);
    print_server_metrics (lib_name, transport, sizes, metrics, queue_stats);
    zlink_close (server);
    return rc;
}
