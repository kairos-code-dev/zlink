// STREAM multi server benchmark: callback-based echo relay.
// Topology: client STREAM(connect, N) -> server STREAM(bind, 1)
// Measurement role: echo incoming framed payloads back to the originating peer.

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"

#include <atomic>
#include <csignal>
#include <cstdint>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace {

static const char *k_pattern = "MULTI_STREAM";
static std::atomic<bool> g_stop_requested (false);
static std::mutex g_stop_mutex;
static std::condition_variable g_stop_cv;

struct stream_handler_context_t
{
    ::perf::socket_t *server;
};

inline void on_signal (int)
{
    g_stop_requested.store (true, std::memory_order_release);
    g_stop_cv.notify_all ();
}

inline void install_signal_handlers ()
{
    std::signal (SIGINT, on_signal);
#if defined(SIGTERM)
    std::signal (SIGTERM, on_signal);
#endif
}

inline zlink::message_t build_packet_frame (zlink_msg_t *header_,
                                            zlink_msg_t *body_)
{
    const size_t header_size = zlink_msg_size (header_);
    const size_t body_size = zlink_msg_size (body_);
    const size_t packet_size = 6 + header_size + body_size;
    zlink::message_t packet (packet_size);
    if (!packet.valid ())
        throw zlink::last_error ();

    unsigned char *dst = static_cast<unsigned char *> (packet.data ());
    dst[0] = static_cast<unsigned char> ((header_size >> 8) & 0xFF);
    dst[1] = static_cast<unsigned char> (header_size & 0xFF);
    dst[2] = static_cast<unsigned char> ((body_size >> 24) & 0xFF);
    dst[3] = static_cast<unsigned char> ((body_size >> 16) & 0xFF);
    dst[4] = static_cast<unsigned char> ((body_size >> 8) & 0xFF);
    dst[5] = static_cast<unsigned char> (body_size & 0xFF);
    if (header_size > 0) {
        std::memcpy (
          dst + 6, zlink_msg_data (header_), header_size);
    }
    if (body_size > 0) {
        std::memcpy (
          dst + 6 + header_size, zlink_msg_data (body_), body_size);
    }
    return packet;
}

void stream_packet_handler (void *,
                            const zlink_routing_id_t *source_rid_,
                            zlink_msg_t *header_,
                            zlink_msg_t *body_,
                            void *userdata_)
{
    stream_handler_context_t *ctx =
      static_cast<stream_handler_context_t *> (userdata_);
    if (!source_rid_ || !header_ || !body_ || !ctx || !ctx->server) {
        if (header_)
            (void) zlink_msg_close (header_);
        if (body_)
            (void) zlink_msg_close (body_);
        g_stop_requested.store (true, std::memory_order_release);
        return;
    }

    try {
        zlink::routing_id_t routing_id = zlink::routing_id_t::from_bytes (
          source_rid_->data, source_rid_->size);
        zlink::message_t packet = build_packet_frame (header_, body_);
        (void) ctx->server->send (routing_id, packet);
    }
    catch (const std::exception &) {
        g_stop_requested.store (true, std::memory_order_release);
    }

    (void) zlink_msg_close (header_);
    (void) zlink_msg_close (body_);
}

void wait_for_stop_stdin ()
{
    std::string line;
    while (std::getline (std::cin, line)) {
        if (line == "STOP" || line == "QUIT") {
            g_stop_requested.store (true, std::memory_order_release);
            g_stop_cv.notify_all ();
            return;
        }
    }

    g_stop_requested.store (true, std::memory_order_release);
    g_stop_cv.notify_all ();
}

} // namespace

bool perf_stream_server (const std::string &lib_name,
                         const std::string &transport,
                         size_t)
{
    perf::multi::set_perf_pattern_env (k_pattern);

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << ","
                  << transport
                  << std::endl;
        return true;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();

    perf::multi::ctx_guard_t ctx;
    perf::multi::socket_guard_t server (ctx, zlink::socket_type::stream);
    if (!server.valid ())
        return false;

    perf::multi::apply_benchmark_socket_options (
      server.sock (), settings, transport);

    const int io_timeout_ms = std::max (settings.sndtimeo_ms, settings.rcvtimeo_ms);
    (void) server.sock ().set_option (zlink::compat::options::socket_options::sndtimeo,
                                      io_timeout_ms);
    (void) server.sock ().set_option (zlink::compat::options::socket_options::rcvtimeo,
                                      io_timeout_ms);
    (void) server.sock ().set_option (zlink::compat::options::socket_options::tcp_nodelay, 1);

    if (!perf::multi::setup_tls_server (server.sock (), transport))
        return false;

    const std::string endpoint = perf::multi::bind_and_resolve_endpoint (
      server.sock (), transport, "cpp_multi_stream",
      settings.server_bind_port);
    if (endpoint.empty ())
        return false;

    g_stop_requested.store (false, std::memory_order_release);
    install_signal_handlers ();

    stream_handler_context_t handler_context = {&server.sock ()};
    if (server.sock ().on_packet (&stream_packet_handler, &handler_context) != 0) {
        return false;
    }

    std::thread stdin_watcher (&wait_for_stop_stdin);
    stdin_watcher.detach ();

    perf::multi::print_ready (endpoint);
    std::unique_lock<std::mutex> stop_lock (g_stop_mutex);
    g_stop_cv.wait (stop_lock, []() {
        return g_stop_requested.load (std::memory_order_acquire);
    });

    perf::multi::print_server_queue_metrics (
      lib_name, k_pattern, transport, 0,
      perf::multi::server_queue_stats_t ());
    return true;
}

int main (int argc, char **argv)
{
    if (argc < 3) {
        std::cerr << "usage: <lib_name> <transport> [size]" << std::endl;
        return 1;
    }

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t size =
      argc >= 4
        ? static_cast<size_t> (std::strtoull (argv[3], NULL, 10))
        : 0;

    return perf_stream_server (lib_name, transport, size) ? 0 : 1;
}
