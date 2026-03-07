#include "../common/perf_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../../../bench/with_zmq/multi/common/bench_resource.hpp"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
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

static const char *k_pattern = "STREAM_CALLBACK";

static std::atomic<bool> g_stop_requested (false);
static std::atomic<bool> g_callback_failed (false);
static void *g_server_socket = NULL;
static std::atomic<bool> g_queue_probe_pending (false);
static std::atomic<size_t> g_queue_probe_size (0);
static std::mutex g_pending_mutex;

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
    (void) data;
    return size == 0;
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

enum send_status_t
{
    send_done = 0,
    send_blocked = 1,
    send_fatal = 2
};

struct pending_stream_message_t
{
    zlink_routing_id_t rid;
    zlink_msg_t payload;
    bool has_payload;

    pending_stream_message_t () : has_payload (false)
    {
        rid.size = 0;
        zlink_msg_init (&payload);
    }

    ~pending_stream_message_t ()
    {
        reset ();
        zlink_msg_close (&payload);
    }

    void reset ()
    {
        if (has_payload) {
            zlink_msg_close (&payload);
            zlink_msg_init (&payload);
            has_payload = false;
        }
        rid.size = 0;
    }

    bool move_from (pending_stream_message_t *other)
    {
        if (!other)
            return false;
        reset ();
        rid = other->rid;
        if (other->has_payload) {
            if (zlink_msg_move (&payload, &other->payload) != 0) {
                rid.size = 0;
                return false;
            }
            has_payload = true;
        }
        other->rid.size = 0;
        other->has_payload = false;
        return true;
    }
};

static pending_stream_message_t *g_pending_messages = NULL;
static size_t g_pending_capacity = 0;
static size_t g_pending_count = 0;

inline send_status_t try_send_stream_message (pending_stream_message_t *message)
{
    if (!g_server_socket || !message || !message->has_payload || message->rid.size == 0)
        return send_fatal;

    const unsigned char *payload =
      static_cast<const unsigned char *> (zlink_msg_data (&message->payload));
    const size_t payload_size = zlink_msg_size (&message->payload);
    const int rc =
      zlink_stream_send (
        g_server_socket, &message->rid, payload, payload_size, ZLINK_DONTWAIT);
    if (rc == static_cast<int> (payload_size)) {
        message->reset ();
        return send_done;
    }
    if (rc >= 0)
        return send_fatal;

    const int err = zlink_errno ();
    if (err == EAGAIN || err == EINTR)
        return send_blocked;
    return send_fatal;
}

inline bool enqueue_pending_stream_message_locked (
  pending_stream_message_t *message)
{
    if (!message || !g_pending_messages || g_pending_count >= g_pending_capacity)
        return false;
    if (!g_pending_messages[g_pending_count].move_from (message))
        return false;
    ++g_pending_count;
    return true;
}

inline void erase_pending_stream_message_locked (size_t idx)
{
    if (!g_pending_messages || idx >= g_pending_count)
        return;
    const size_t last = g_pending_count - 1;
    g_pending_messages[idx].reset ();
    if (idx != last)
        (void) g_pending_messages[idx].move_from (&g_pending_messages[last]);
    g_pending_messages[last].reset ();
    --g_pending_count;
}

inline bool flush_pending_stream_messages_locked ()
{
    size_t idx = 0;
    while (idx < g_pending_count) {
        const send_status_t send_rc =
          try_send_stream_message (&g_pending_messages[idx]);
        if (send_rc == send_done) {
            erase_pending_stream_message_locked (idx);
            continue;
        }
        if (send_rc == send_fatal)
            return false;
        ++idx;
    }
    return true;
}

int on_stream_packet (const zlink_routing_id_t *rid, zlink_msg_t *msg)
{
    if (!rid || !msg || !g_server_socket)
        return 0;

    const unsigned char *payload =
      static_cast<const unsigned char *> (zlink_msg_data (msg));
    const size_t payload_size = zlink_msg_size (msg);
    if (is_stream_event_payload (payload, payload_size)) {
        (void) zlink_msg_close (msg);
        return 0;
    }

    pending_stream_message_t request;
    request.rid = *rid;
    if (zlink_msg_move (&request.payload, msg) != 0) {
        g_callback_failed.store (true, std::memory_order_release);
        (void) zlink_msg_close (msg);
        return 1;
    }
    request.has_payload = true;
    const send_status_t send_rc = try_send_stream_message (&request);
    if (send_rc == send_done) {
        (void) zlink_msg_close (msg);
        return 0;
    }
    if (send_rc == send_blocked) {
        std::lock_guard<std::mutex> guard (g_pending_mutex);
        if (!enqueue_pending_stream_message_locked (&request)) {
            g_callback_failed.store (true, std::memory_order_release);
            (void) zlink_msg_close (msg);
            return 1;
        }
        (void) zlink_msg_close (msg);
        return 0;
    }

    if (send_rc == send_fatal) {
        g_callback_failed.store (true, std::memory_order_release);
        (void) zlink_msg_close (msg);
        return 1;
    }

    return 0;
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
      server, transport, lib_name + "_stream_callback_server");
    if (endpoint.empty ()) {
        zlink_close (server);
        return 1;
    }

    g_stop_requested.store (false, std::memory_order_release);
    g_callback_failed.store (false, std::memory_order_release);
    g_queue_probe_pending.store (false, std::memory_order_release);
    g_queue_probe_size.store (0, std::memory_order_release);
    g_server_socket = server;
    g_pending_capacity =
      std::max<size_t> (
        64,
        std::max<size_t> (
          settings.clients,
          static_cast<size_t> (settings.hwm > 0 ? settings.hwm : 1))
          * 2);
    g_pending_messages = new pending_stream_message_t[g_pending_capacity];
    g_pending_count = 0;
    install_signal_handlers ();

    if (zlink_stream_attach_raw (server, on_stream_packet) != 0) {
        g_server_socket = NULL;
        zlink_close (server);
        return 1;
    }

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
        if (g_callback_failed.load (std::memory_order_acquire)) {
            rc = 1;
            break;
        }

        bool have_pending = false;
        {
            std::lock_guard<std::mutex> guard (g_pending_mutex);
            have_pending = g_pending_count > 0;
        }
        if (!have_pending) {
            if (zlink_poll (NULL, 0, 50) < 0 && zlink_errno () != EINTR) {
                rc = 1;
                break;
            }
            continue;
        }

        zlink_pollitem_t item = {server, 0, ZLINK_POLLOUT, 0};
        const int prc = zlink_poll (&item, 1, 50);
        if (prc < 0) {
            if (zlink_errno () == EINTR)
                continue;
            rc = 1;
            break;
        }
        if (prc > 0 && (item.revents & ZLINK_POLLOUT) != 0) {
            std::lock_guard<std::mutex> guard (g_pending_mutex);
            if (!flush_pending_stream_messages_locked ()) {
                rc = 1;
                break;
            }
        }
    }

    (void) zlink_stream_detach (server);
    delete[] g_pending_messages;
    g_pending_messages = NULL;
    g_pending_capacity = 0;
    g_pending_count = 0;
    g_server_socket = NULL;

    const bench_resource_metrics_t metrics =
      bench_finish_resource_probe (cpu_start);
    const server_queue_stats_t queue_stats =
      sample_server_queue_stats (server, server);
    print_server_metrics (lib_name, transport, sizes, metrics, queue_stats);
    zlink_close (server);
    return rc;
}
