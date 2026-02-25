#include "../common/perf_multi_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <deque>
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

static const char *k_pattern = "MULTI_STREAM_CALLBACK";
static const int k_dispatch_flags = 0;

static std::atomic<bool> g_stop_requested (false);
static std::atomic<bool> g_callback_failed (false);
static void *g_server_socket = NULL;

struct pending_reply_t
{
    zlink_routing_id_t rid;
    std::vector<unsigned char> payload;
};

static std::mutex g_pending_mu;
static std::condition_variable g_pending_cv;
static std::deque<pending_reply_t> g_pending_replies;

inline void on_signal (int)
{
    g_stop_requested.store (true, std::memory_order_release);
    g_pending_cv.notify_all ();
}

inline void install_signal_handlers ()
{
    std::signal (SIGINT, on_signal);
#if defined(SIGTERM)
    std::signal (SIGTERM, on_signal);
#endif
}

inline bool is_stream_event_payload (const unsigned char *data, size_t size)
{
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
      resolve_multi_int_env ("PERF_MULTI_SERVER_BIND_PORT", 0, 0);
    if (bind_port <= 0)
        return bind_and_resolve_endpoint (server, transport, token);

    std::string endpoint = make_fixed_endpoint (transport, bind_port);
    if (zlink_bind (server, endpoint.c_str ()) != 0) {
        std::cerr << "bind failed for " << endpoint << ": "
                  << zlink_strerror (zlink_errno ()) << std::endl;
        return std::string ();
    }

    char last_endpoint[MAX_SOCKET_STRING] = "";
    size_t size = sizeof (last_endpoint);
    if (zlink_getsockopt (server, ZLINK_LAST_ENDPOINT, last_endpoint, &size)
        == 0)
        endpoint.assign (last_endpoint);
    apply_debug_timeouts (server, transport);
    return endpoint;
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

inline bool send_stream_with_retry (const zlink_routing_id_t *rid,
                                    const unsigned char *payload,
                                    size_t payload_size)
{
    if (!g_server_socket || !rid)
        return false;

    const int retry_limit =
      resolve_multi_int_env ("PERF_MULTI_STREAM_SEND_RETRIES", 256, 1);
    const int retry_backoff_us =
      resolve_multi_int_env ("PERF_MULTI_SEND_BACKOFF_US", 20, 0);

    for (int attempt = 0; attempt < retry_limit; ++attempt) {
        const int rc =
          zlink_stream_send (g_server_socket, rid, payload, payload_size, 0);
        if (rc == static_cast<int> (payload_size))
            return true;

        if (rc < 0) {
            const int err = zlink_errno ();
            if (err == ECONNRESET || err == EHOSTUNREACH || err == ENOTCONN
                || err == EPIPE)
                return true;
            if (err == EAGAIN || err == EINTR) {
                if (retry_backoff_us > 0) {
                    std::this_thread::sleep_for (
                      std::chrono::microseconds (retry_backoff_us));
                }
                continue;
            }
        }
        return false;
    }

    return false;
}

inline bool enqueue_reply (const zlink_routing_id_t *rid,
                           const unsigned char *payload,
                           size_t payload_size)
{
    if (!rid || rid->size == 0 || rid->size > sizeof (rid->data))
        return false;

    pending_reply_t reply;
    std::memset (&reply.rid, 0, sizeof (reply.rid));
    reply.rid.size = rid->size;
    std::memcpy (reply.rid.data, rid->data, rid->size);
    if (payload_size > 0 && payload)
        reply.payload.assign (payload, payload + payload_size);

    {
        std::lock_guard<std::mutex> lk (g_pending_mu);
        g_pending_replies.push_back (std::move (reply));
    }
    g_pending_cv.notify_one ();
    return true;
}

int on_stream_packets (const zlink_routing_id_t *rid,
                       zlink_msg_t *msgs,
                       size_t msg_count)
{
    if (!rid || !msgs || msg_count == 0 || !g_server_socket)
        return 0;

    for (size_t i = 0; i < msg_count; ++i) {
        zlink_msg_t *msg = &msgs[i];
        const unsigned char *payload =
          static_cast<const unsigned char *> (zlink_msg_data (msg));
        const size_t payload_size = zlink_msg_size (msg);
        if (is_stream_event_payload (payload, payload_size))
            continue;
        if (!enqueue_reply (rid, payload, payload_size)) {
            g_callback_failed.store (true, std::memory_order_release);
            g_pending_cv.notify_all ();
            return 1;
        }
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
    set_perf_multi_pattern_env (k_pattern);

    if (!transport_available (transport) || !is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << ","
                  << transport << std::endl;
        return 0;
    }

    ctx_guard_t ctx;
    if (!ctx.valid ())
        return 1;

    void *server = zlink_socket (ctx.get (), ZLINK_STREAM);
    if (!server)
        return 1;

    const bench_multi_cpu_sample_t cpu_start = bench_multi_capture_cpu_sample ();
    const multi_bench_settings_t settings = resolve_multi_bench_settings ();
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

    const std::string endpoint =
      bind_server_endpoint (server, transport, lib_name + "_multi_stream_callback_server");
    if (endpoint.empty ()) {
        zlink_close (server);
        return 1;
    }

    {
        std::lock_guard<std::mutex> lk (g_pending_mu);
        g_pending_replies.clear ();
    }
    g_stop_requested.store (false, std::memory_order_release);
    g_callback_failed.store (false, std::memory_order_release);
    g_server_socket = server;
    install_signal_handlers ();

    if (zlink_stream_attach (server, on_stream_packets, k_dispatch_flags) != 0) {
        g_server_socket = NULL;
        zlink_close (server);
        return 1;
    }

    std::thread stdin_watcher ([] () {
        std::string line;
        while (std::getline (std::cin, line)) {
            if (line == "STOP" || line == "QUIT") {
                g_stop_requested.store (true, std::memory_order_release);
                g_pending_cv.notify_all ();
                return;
            }
        }
        g_stop_requested.store (true, std::memory_order_release);
        g_pending_cv.notify_all ();
    });
    stdin_watcher.detach ();

    std::cout << "READY," << endpoint << std::endl;

    int rc = 0;
    while (!g_stop_requested.load (std::memory_order_acquire)) {
        if (g_callback_failed.load (std::memory_order_acquire)) {
            rc = 1;
            break;
        }

        pending_reply_t reply;
        bool has_reply = false;
        {
            std::unique_lock<std::mutex> lk (g_pending_mu);
            if (g_pending_replies.empty ()) {
                g_pending_cv.wait_for (lk, std::chrono::milliseconds (1));
            }
            if (!g_pending_replies.empty ()) {
                reply = std::move (g_pending_replies.front ());
                g_pending_replies.pop_front ();
                has_reply = true;
            }
        }

        if (!has_reply)
            continue;

        const unsigned char *reply_data =
          reply.payload.empty () ? NULL : &reply.payload[0];
        if (!send_stream_with_retry (&reply.rid, reply_data, reply.payload.size ())) {
            g_callback_failed.store (true, std::memory_order_release);
            rc = 1;
            break;
        }
    }

    g_server_socket = NULL;
    const bench_multi_resource_metrics_t metrics =
      bench_multi_finish_resource_probe (cpu_start);
    print_server_metrics (lib_name, transport, sizes, metrics);
    zlink_close (server);
    return rc;
}
