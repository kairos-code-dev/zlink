#include "../common/perf_multi_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"

#include <atomic>
#include <cerrno>
#include <csignal>
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

static const char *k_pattern = "MULTI_STREAM";
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
    if (zlink_getsockopt (server, ZLINK_LAST_ENDPOINT, last_endpoint, &size) == 0)
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

inline bool read_stream_frame (void *server,
                               zlink_routing_id_t *rid,
                               std::vector<unsigned char> *payload,
                               bool *timed_out)
{
    if (!server || !rid || !payload || !timed_out)
        return false;

    *timed_out = false;

    zlink_msg_t id_msg;
    zlink_msg_init (&id_msg);
    const int id_size = zlink_msg_recv (&id_msg, server, 0);
    if (id_size <= 0) {
        const int err = zlink_errno ();
        zlink_msg_close (&id_msg);
        if (err == EAGAIN || err == EINTR)
            *timed_out = true;
        return false;
    }

    int more = 0;
    size_t more_size = sizeof (more);
    if (zlink_getsockopt (server, ZLINK_RCVMORE, &more, &more_size) != 0 || !more) {
        zlink_msg_close (&id_msg);
        return false;
    }

    zlink_msg_t payload_msg;
    zlink_msg_init (&payload_msg);
    const int payload_size = zlink_msg_recv (&payload_msg, server, 0);
    if (payload_size < 0) {
        const int err = zlink_errno ();
        zlink_msg_close (&payload_msg);
        zlink_msg_close (&id_msg);
        if (err == EAGAIN || err == EINTR)
            *timed_out = true;
        return false;
    }

    std::memset (rid, 0, sizeof (*rid));
    rid->size = static_cast<uint8_t> (
      std::min<int> (id_size, static_cast<int> (sizeof (rid->data))));
    if (rid->size > 0) {
        std::memcpy (rid->data, zlink_msg_data (&id_msg), rid->size);
    }

    payload->clear ();
    if (payload_size > 0) {
        const unsigned char *data =
          static_cast<const unsigned char *> (zlink_msg_data (&payload_msg));
        payload->assign (data, data + payload_size);
    }

    zlink_msg_close (&payload_msg);
    zlink_msg_close (&id_msg);
    return true;
}

inline bool send_stream_once (void *server,
                              const zlink_routing_id_t *rid,
                              const unsigned char *payload,
                              size_t payload_size)
{
    if (!server || !rid || rid->size == 0)
        return false;

    const int rc = zlink_stream_send (server, rid, payload, payload_size, 0);
    if (rc == static_cast<int> (payload_size))
        return true;

    if (rc < 0) {
        const int err = zlink_errno ();
        if (err == ECONNRESET || err == EHOSTUNREACH || err == ENOTCONN
            || err == EPIPE)
            return true;
    }
    return false;
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
      bind_server_endpoint (server, transport, lib_name + "_multi_stream_server");
    if (endpoint.empty ()) {
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

    std::cout << "READY," << endpoint << std::endl;

    int rc = 0;
    while (!g_stop_requested.load (std::memory_order_acquire)) {
        zlink_routing_id_t rid;
        std::vector<unsigned char> payload;
        bool timed_out = false;
        if (!read_stream_frame (server, &rid, &payload, &timed_out)) {
            if (timed_out)
                continue;
            rc = 1;
            break;
        }

        if (is_stream_event_payload (payload.empty () ? NULL : &payload[0],
                                     payload.size ())) {
            continue;
        }

        const unsigned char *reply_data = payload.empty () ? NULL : &payload[0];
        if (!send_stream_once (server, &rid, reply_data, payload.size ())) {
            rc = 1;
            break;
        }
    }

    const bench_multi_resource_metrics_t metrics =
      bench_multi_finish_resource_probe (cpu_start);
    print_server_metrics (lib_name, transport, sizes, metrics);
    zlink_close (server);
    return rc;
}
