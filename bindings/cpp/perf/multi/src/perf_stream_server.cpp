// STREAM multi server benchmark: raw stream callback echo.
// Topology: core stream client(connect, N) <-> server STREAM(bind, 1)
// Measurement role: callback receives frame and echoes bytes to routing id.

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"

#include <zlink/compat.hpp>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <thread>

namespace {

std::atomic<zlink::socket_t *> g_stream_socket (NULL);
std::atomic<bool> g_stop_requested (false);
std::atomic<bool> g_callback_failed (false);

bool is_stream_event_payload (const unsigned char *data, size_t size)
{
    if (size == 0)
        return true;
    return data && size == 1 && (data[0] == 0x00 || data[0] == 0x01);
}

bool send_stream_once (const zlink_routing_id_t &rid,
                       const void *payload,
                       size_t payload_size)
{
    zlink::socket_t *stream_socket = g_stream_socket.load (std::memory_order_acquire);
    if (!stream_socket || rid.size == 0 || !payload)
        return false;

    const int rc =
      stream_socket->stream_send (
        rid, payload, payload_size, zlink::send_flag::none);
    return rc == static_cast<int> (payload_size);
}

int on_stream_raw (const zlink_routing_id_t *rid, zlink_msg_t *msg)
{
    if (!msg)
        return 0;

    void *data = zlink::compat::msg_data (msg);
    const size_t size = zlink::compat::msg_size (msg);
    const unsigned char *payload =
      static_cast<const unsigned char *> (data);

    if (is_stream_event_payload (payload, size)) {
        (void) zlink::compat::msg_close (msg);
        return 0;
    }

    const bool stop = perf::multi::is_stop_token (data, size);

    if (!stop) {
        if (!rid
            || !g_stream_socket.load (std::memory_order_acquire)) {
            (void) zlink::compat::msg_close (msg);
            return 0;
        }

        if (!send_stream_once (*rid, data, size)) {
            (void) zlink::compat::msg_close (msg);
            g_callback_failed.store (true);
            return 1;
        }
    }

    (void) zlink::compat::msg_close (msg);
    if (stop) {
        g_stop_requested.store (true);
        return 1;
    }
    return 0;
}

} // namespace

void perf_stream_server (const std::string &transport, size_t msg_size)
{
    perf::multi::set_perf_pattern_env ("STREAM");

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED,MULTI_STREAM," << transport << std::endl;
        return;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();

    perf::multi::ctx_guard_t ctx;
    perf::multi::socket_guard_t server (ctx, zlink::socket_type::stream);
    if (!server.valid ())
        return;

    perf::multi::apply_benchmark_socket_options (
      server.sock (), settings, transport);
    if (!perf::multi::setup_tls_server (server.sock (), transport))
        return;

    const std::string endpoint = perf::multi::bind_and_resolve_endpoint (
      server.sock (), transport, "cpp_multi_stream", settings.server_bind_port);
    if (endpoint.empty ())
        return;

    g_stream_socket.store (&server.sock (), std::memory_order_release);
    g_stop_requested.store (false);
    g_callback_failed.store (false);
    if (server.sock ().stream_attach (&on_stream_raw) != 0) {
        g_stream_socket.store (NULL, std::memory_order_release);
        return;
    }

    perf::multi::print_ready (endpoint);

    while (!g_stop_requested.load () && !g_callback_failed.load ()) {
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    (void) server.sock ().stream_detach ();
    g_stream_socket.store (NULL, std::memory_order_release);

    perf::multi::print_server_queue_metrics (
      "current", "MULTI_STREAM", transport, msg_size, perf::multi::server_queue_stats_t ());
}

int main (int argc, char **argv)
{
    if (argc < 3) {
        std::cerr << "usage: <transport> <size>" << std::endl;
        return 1;
    }

    const std::string transport = argv[1];
    const size_t size = static_cast<size_t> (std::strtoull (argv[2], NULL, 10));
    if (size == 0)
        return 1;

    perf_stream_server (transport, size);
    return 0;
}
