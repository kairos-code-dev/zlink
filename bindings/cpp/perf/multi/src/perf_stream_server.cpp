// STREAM multi server benchmark: poll-based echo relay.
// Topology: client STREAM(connect, N) -> server STREAM(bind, 1)
// Measurement role: echo incoming payloads back to the originating peer.

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"
#include "../common/perf_stream_session.hpp"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

static const char *k_pattern = "MULTI_STREAM";
static const char k_stop_token[] = "__zlink_perf_stop__";

static perf::multi::stream_session_t g_stream_session;

inline void on_signal (int)
{
    g_stream_session.stop_requested.store (true, std::memory_order_release);
}

inline void install_signal_handlers ()
{
    std::signal (SIGINT, on_signal);
#if defined(SIGTERM)
    std::signal (SIGTERM, on_signal);
#endif
}

inline void emit_requested_queue_probe (const std::string &transport)
{
    perf::multi::emit_requested_queue_probe (&g_stream_session,
                                             k_pattern,
                                             transport);
}

} // namespace

bool perf_stream_server (const std::string &transport, size_t)
{
    perf::multi::set_perf_pattern_env (k_pattern);

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED,current," << k_pattern << "," << transport
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

    const int io_timeout_ms =
      perf::multi::parse_positive_env ("PERF_STREAM_TIMEOUT_MS", 5000);
    (void) server.sock ().set_option (zlink::socket_options::sndtimeo,
                                      io_timeout_ms);
    (void) server.sock ().set_option (zlink::socket_options::rcvtimeo,
                                      io_timeout_ms);
    (void) server.sock ().set_option (zlink::socket_options::tcp_nodelay, 1);

    if (!perf::multi::setup_tls_server (server.sock (), transport))
        return false;

    const std::string endpoint = perf::multi::bind_and_resolve_endpoint (
      server.sock (), transport, "cpp_multi_stream",
      settings.server_bind_port);
    if (endpoint.empty ())
        return false;

    g_stream_session.stop_requested.store (false, std::memory_order_release);
    g_stream_session.queue_probe_pending.store (false, std::memory_order_release);
    g_stream_session.queue_probe_size.store (0, std::memory_order_release);
    g_stream_session.server_socket = &server.sock ();
    install_signal_handlers ();
    perf::multi::start_control_stdin_watcher (&g_stream_session);

    perf::multi::print_ready (endpoint);

    int rc = 0;
    zlink::poller_t poller;
    if (!poller.valid ()) {
        return false;
    }
    try {
        poller.add (server.sock (), zlink::poll_event::pollin);
    }
    catch (const zlink::zlink_error_t &) {
        return false;
    }
    g_stream_session.pending_capacity = std::max<size_t> (
      64,
      std::max<size_t> (
        settings.clients,
        static_cast<size_t> (settings.hwm > 0 ? settings.hwm : 1))
        * 2);
    g_stream_session.pending_messages =
      new perf::multi::pending_stream_message_t[g_stream_session.pending_capacity];
    g_stream_session.pending_count = 0;
    while (!g_stream_session.stop_requested.load (std::memory_order_acquire)) {
        emit_requested_queue_probe (transport);
        const bool want_send = g_stream_session.pending_count > 0;
        const zlink::poll_event wait_events =
          want_send ? (zlink::poll_event::pollin | zlink::poll_event::pollout)
                    : zlink::poll_event::pollin;
        try {
            poller.modify (server.sock (), wait_events);
        }
        catch (const zlink::zlink_error_t &) {
            rc = 1;
            break;
        }

        zlink::poll_event_t event = {};
        const int prc = poller.wait (&event, 50);
        if (prc < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            rc = 1;
            break;
        }
        if ((event.revents & static_cast<short> (zlink::poll_event::pollout)) != 0
            && g_stream_session.pending_count > 0) {
            if (!perf::multi::flush_pending_locked (&g_stream_session)) {
                rc = 1;
                break;
            }
        }
        if (prc == 0
            || (event.revents & static_cast<short> (zlink::poll_event::pollin))
                 == 0) {
            continue;
        }

        while (!g_stream_session.stop_requested.load (std::memory_order_acquire)) {
            zlink::received_t received;
            int recv_rc =
              server.sock ().receive (received, zlink::recv_flag::dontwait);
            while (recv_rc < 0 && errno == EINTR)
                recv_rc =
                  server.sock ().receive (received, zlink::recv_flag::dontwait);
            if (recv_rc < 0) {
                const int err = errno;
                if (err == EAGAIN || err == EINTR)
                    break;
                rc = 1;
                break;
            }
            const bool ok = perf::multi::process_stream_recv_parts (
              &g_stream_session, received, k_stop_token);
            if (!ok) {
                rc = 1;
                break;
            }
        }
        if (rc != 0)
            break;
    }

    perf::multi::clear_session (&g_stream_session);

    perf::multi::print_server_queue_metrics (
      "current", k_pattern, transport, 0,
      perf::multi::server_queue_stats_t ());
    return rc == 0;
}

int main (int argc, char **argv)
{
    if (argc < 2) {
        std::cerr << "usage: <transport> [size]" << std::endl;
        return 1;
    }

    const std::string transport = argv[1];
    const size_t size =
      argc >= 3
        ? static_cast<size_t> (std::strtoull (argv[2], NULL, 10))
        : 0;

    return perf_stream_server (transport, size) ? 0 : 1;
}
