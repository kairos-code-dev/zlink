#include "../common/perf_multi_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifndef ZLINK_STREAM
#define ZLINK_STREAM 11
#endif

#ifndef ZLINK_TCP_NODELAY
#define ZLINK_TCP_NODELAY 26
#endif

namespace {

static const char *k_pattern = "MULTI_STREAM_CALLBACK";
static const uint32_t k_max_stream_payload = 4u * 1024u * 1024u;

static std::atomic<bool> g_stop_requested (false);
static std::atomic<bool> g_callback_failed (false);
static void *g_server_socket = NULL;
struct stream_reassembly_state_t
{
    std::vector<unsigned char> bytes;
    size_t offset;

    stream_reassembly_state_t () : bytes (), offset (0) {}
};
static std::unordered_map<std::string, stream_reassembly_state_t> g_stream_reassembly;
static std::mutex g_stream_reassembly_mutex;

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

inline uint32_t load_u32_be (const unsigned char *p)
{
    return (static_cast<uint32_t> (p[0]) << 24)
           | (static_cast<uint32_t> (p[1]) << 16)
           | (static_cast<uint32_t> (p[2]) << 8)
           | static_cast<uint32_t> (p[3]);
}

inline std::string routing_id_key (const zlink_routing_id_t *rid)
{
    if (!rid || rid->size == 0)
        return std::string ();
    const char *data = reinterpret_cast<const char *> (&rid->data[0]);
    return std::string (data, data + rid->size);
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

inline bool send_stream_once (const zlink_routing_id_t *rid,
                              const unsigned char *payload,
                              size_t payload_size)
{
    if (!g_server_socket || !rid || rid->size == 0)
        return false;

    const int rc = zlink_stream_send (g_server_socket, rid, payload, payload_size, 0);
    if (rc == static_cast<int> (payload_size))
        return true;

    if (rc < 0) {
        const int err = zlink_errno ();
        if (err == ECONNRESET || err == EHOSTUNREACH || err == ENOTCONN
            || err == EPIPE || err == EAGAIN || err == EINTR)
            return true;
    }
    return false;
}

inline bool append_and_echo_len32be_frames (const zlink_routing_id_t *rid,
                                            const unsigned char *payload,
                                            size_t payload_size)
{
    if (!rid || !payload || payload_size == 0)
        return true;

    const std::string key = routing_id_key (rid);
    if (key.empty ())
        return false;

    std::lock_guard<std::mutex> lock (g_stream_reassembly_mutex);
    stream_reassembly_state_t &state = g_stream_reassembly[key];
    state.bytes.insert (state.bytes.end (), payload, payload + payload_size);

    while (true) {
        const size_t available = state.bytes.size () - state.offset;
        if (available < 4)
            break;

        const unsigned char *frame = &state.bytes[state.offset];
        const uint32_t declared = load_u32_be (frame);
        if (declared > k_max_stream_payload) {
            g_stream_reassembly.erase (key);
            return false;
        }

        const size_t total = static_cast<size_t> (4 + declared);
        if (available < total)
            break;

        if (!send_stream_once (rid, frame, total)) {
            g_stream_reassembly.erase (key);
            return false;
        }

        state.offset += total;
    }

    if (state.offset > 0) {
        if (state.offset >= state.bytes.size ()) {
            state.bytes.clear ();
            state.offset = 0;
        } else if (state.offset >= 4096 || state.offset * 2 >= state.bytes.size ()) {
            state.bytes.erase (state.bytes.begin (),
                               state.bytes.begin ()
                                 + static_cast<std::ptrdiff_t> (state.offset));
            state.offset = 0;
        }
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
        const std::string key = routing_id_key (rid);
        if (!key.empty ()) {
            std::lock_guard<std::mutex> lock (g_stream_reassembly_mutex);
            g_stream_reassembly.erase (key);
        }
        (void) zlink_msg_close (msg);
        return 0;
    }

    if (!append_and_echo_len32be_frames (rid, payload, payload_size)) {
        g_callback_failed.store (true, std::memory_order_release);
        (void) zlink_msg_close (msg);
        return 1;
    }

    (void) zlink_msg_close (msg);
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

    const std::string endpoint = bind_server_endpoint (
      server, transport, lib_name + "_multi_stream_callback_server");
    if (endpoint.empty ()) {
        zlink_close (server);
        return 1;
    }

    g_stop_requested.store (false, std::memory_order_release);
    g_callback_failed.store (false, std::memory_order_release);
    g_server_socket = server;
    install_signal_handlers ();

    if (zlink_stream_attach_raw (server, on_stream_packet) != 0) {
        g_server_socket = NULL;
        zlink_close (server);
        return 1;
    }

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
        if (g_callback_failed.load (std::memory_order_acquire)) {
            rc = 1;
            break;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    (void) zlink_stream_detach (server);
    g_server_socket = NULL;
    {
        std::lock_guard<std::mutex> lock (g_stream_reassembly_mutex);
        g_stream_reassembly.clear ();
    }

    const bench_multi_resource_metrics_t metrics =
      bench_multi_finish_resource_probe (cpu_start);
    print_server_metrics (lib_name, transport, sizes, metrics);
    zlink_close (server);
    return rc;
}
