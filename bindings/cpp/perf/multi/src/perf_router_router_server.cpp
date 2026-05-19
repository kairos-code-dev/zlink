// ROUTER-ROUTER multi server benchmark: routed echo responder.
// Topology: client ROUTER(connect, N) <-> server ROUTER(bind, routing_id=SERVER)
// Measurement role: echo payload to sender routing id and emit queue metrics.

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <deque>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

// Termination mirrors the C reference relay server
// (bindings/c/perf/multi/common/perf_multi_relay_server.hpp): the server has
// NO socket stop-token path; it stops only via the stdin STOP/QUIT watcher
// (run_comparison.py writes "STOP\n" then closes stdin) and SIGINT/SIGTERM
// (run_comparison.py terminate() fallback that interrupts the blocked poll).
static std::atomic<bool> g_stop_requested (false);

inline void request_stop ()
{
    g_stop_requested.store (true, std::memory_order_release);
}

inline void on_signal (int)
{
    request_stop ();
}

inline void install_signal_handlers ()
{
    std::signal (SIGINT, on_signal);
#if defined(SIGTERM)
    std::signal (SIGTERM, on_signal);
#endif
}

inline void wait_for_stop_stdin ()
{
    std::string line;
    while (std::getline (std::cin, line)) {
        if (line == "STOP" || line == "QUIT") {
            request_stop ();
            return;
        }
    }
    // stdin EOF (run_comparison.py closed the pipe) also means stop.
    request_stop ();
}

bool perf_debug_enabled ()
{
    return std::getenv ("PERF_DEBUG") != NULL;
}

void debug_log (const std::string &message_)
{
    if (!perf_debug_enabled ())
        return;
    std::cerr << "router_router server: " << message_ << std::endl;
}

} // namespace

bool perf_router_router_server (const std::string &lib_name,
                                const std::string &transport,
                                size_t msg_size)
{
    perf::multi::set_perf_pattern_env ("ROUTER_ROUTER");

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << ",MULTI_ROUTER_ROUTER,"
                  << transport
                  << std::endl;
        return true;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();

    perf::multi::ctx_guard_t ctx;
    // Hold the typed router_socket_t directly; matches dealer_router_server
    // and avoids the perf::socket_t variant visit overhead on every hot
    // send/recv (cpp.md round 21).
    zlink::router_socket_t server (ctx.ctx ());
    if (!server.valid ())
        return false;

    server.set_routing_id (
      zlink::routing_id_t::from_bytes (
        reinterpret_cast<const uint8_t *> ("SERVER"), 6));
    perf::multi::apply_benchmark_socket_options (server, settings, transport);
    if (!perf::multi::apply_benchmark_auto_hwm_msg_unit (ctx, msg_size))
        return false;
    if (!perf::multi::setup_tls_server (server, transport))
        return false;

    const std::string endpoint = perf::multi::bind_and_resolve_endpoint (
      server, transport, "cpp_multi_router_router",
      settings.server_bind_port);
    if (endpoint.empty ())
        return false;
    if (!perf::multi::recalculate_auto_hwm (ctx))
        return false;
    perf::multi::emit_auto_hwm_detail (
      server, "server", "server", transport, msg_size, "router");

    g_stop_requested.store (false, std::memory_order_release);
    install_signal_handlers ();
    std::thread stdin_watcher (&wait_for_stop_stdin);
    stdin_watcher.detach ();

    perf::multi::print_ready (endpoint);

    struct pending_reply_t
    {
        zlink_routing_id_t rid;
        zlink::message_t payload;
    };

    bool failed = false;
    std::deque<pending_reply_t> pending_replies;
    zlink::poller_t poller;
    std::vector<zlink::poll_event_t> events;
    zlink::message_t part;
    events.reserve (1);
    poller.add (server, zlink::poll_event_flag_t::pollin);

    auto try_send_native_rid =
      [&] (const zlink_routing_id_t *rid, zlink::message_t &payload,
           bool *would_block) -> bool {
        if (would_block)
            *would_block = false;
        const int send_rc = zlink_send_part_rid (
          zlink::detail::native_handle (server),
          rid,
          zlink::detail::native_handle (payload),
          ZLINK_DONTWAIT,
          ZLINK_PART_FINAL);
        if (send_rc == 0) {
            zlink::detail::mark_sent (payload);
            return true;
        }

        const int err_no = errno;
        if (err_no == EAGAIN || err_no == EWOULDBLOCK || err_no == EINTR
            || err_no == EHOSTUNREACH || err_no == ENOTCONN) {
            if (would_block)
                *would_block = true;
            errno = err_no;
            return false;
        }

        errno = err_no;
        return false;
      };

    auto flush_pending = [&] () -> bool {
        while (!pending_replies.empty ()) {
            pending_reply_t &front = pending_replies.front ();
            bool would_block = false;
            if (try_send_native_rid (&front.rid, front.payload, &would_block)) {
                pending_replies.pop_front ();
                continue;
            }
            return would_block;
        }
        return true;
    };

    int poll_event_mask =
      static_cast<int> (zlink::poll_event_flag_t::pollin);
    // Bounded poll wait so the stdin/signal stop flag is observed promptly
    // without depending solely on a SIGTERM interrupting an infinite poll
    // (the C reference uses -1 + SIGTERM; this is the equivalent outcome,
    // just more responsive). 200ms keeps idle wakeups negligible.
    const std::chrono::milliseconds poll_timeout (200);
    while (!g_stop_requested.load (std::memory_order_acquire)) {
        const zlink::poll_event_flag_t mask =
          pending_replies.empty ()
            ? zlink::poll_event_flag_t::pollin
            : static_cast<zlink::poll_event_flag_t> (
                static_cast<int> (zlink::poll_event_flag_t::pollin)
                | static_cast<int> (zlink::poll_event_flag_t::pollout));
        const int next_mask = static_cast<int> (mask);
        if (next_mask != poll_event_mask) {
            poller.modify (server, mask);
            poll_event_mask = next_mask;
        }

        try {
            poller.wait (events, 1, poll_timeout);
        }
        catch (const zlink::zlink_error_t &err) {
            const int err_no = err.internal_errno ();
            if (err_no == EINTR)
                continue;
            debug_log ("poll failed errno=" + std::to_string (err_no));
            failed = true;
            break;
        }
        if (events.empty ())
            continue;

        const auto revents_value = static_cast<int> (events[0].revents);
        const bool readable = (revents_value
                               & static_cast<int> (
                                 zlink::poll_event_flag_t::pollin))
                              != 0;
        const bool writable = (revents_value
                               & static_cast<int> (
                                 zlink::poll_event_flag_t::pollout))
                              != 0;

        if (writable && !pending_replies.empty ()) {
            if (!flush_pending ()) {
                failed = true;
                break;
            }
        }

        if (!readable)
            continue;

        // Drain available single-part routed messages without blocking. This
        // mirrors the C relay server's zlink_router_recv_part hot path and
        // avoids materializing received_t parts for the single-part echo
        // workload.
        while (!g_stop_requested.load (std::memory_order_acquire)) {
            part.init ();
            const zlink_routing_id_t *source_rid = NULL;
            const zlink_routing_id_t *source_spot_rid = NULL;
            uint64_t request_seq = 0;
            zlink_part_flag_t has_more = ZLINK_PART_FINAL;
            const int recv_rc = zlink_router_recv_part (
              zlink::detail::native_handle (server),
              &source_rid,
              &source_spot_rid,
              &request_seq,
              zlink::detail::native_handle (part),
              &has_more,
              ZLINK_RECV_FLAGS_DONTWAIT);
            if (recv_rc != 0) {
                const int err = errno;
                part.close ();
                if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR)
                    break;
                debug_log ("recv failed errno=" + std::to_string (err));
                failed = true;
                break;
            }
            if (!source_rid || source_rid->size == 0
                || (source_spot_rid && source_spot_rid->size != 0)
                || request_seq != 0 || has_more != ZLINK_PART_FINAL) {
                part.close ();
                failed = true;
                break;
            }

            // No socket stop-token handling: matches the C reference relay
            // server, which terminates only via stdin STOP/QUIT + signals.
            if (part.size () == 0) {
                part.close ();
                continue;
            }

            if (!pending_replies.empty ()) {
                pending_replies.push_back (pending_reply_t {
                  *source_rid, std::move (part) });
                continue;
            }

            bool would_block = false;
            if (!try_send_native_rid (source_rid, part, &would_block)) {
                if (would_block) {
                    pending_replies.push_back (pending_reply_t {
                      *source_rid, std::move (part) });
                    continue;
                }
                debug_log ("send failed errno=" + std::to_string (errno));
                failed = true;
                break;
            }
        }
        if (failed)
            break;
    }

    return !failed;
}

int main (int argc, char **argv)
{
    if (argc < 4) {
        std::cerr << "usage: <lib_name> <transport> <size>" << std::endl;
        return 1;
    }

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t size = static_cast<size_t> (std::strtoull (argv[3], NULL, 10));
    if (size == 0)
        return 1;

    return perf_router_router_server (lib_name, transport, size) ? 0 : 1;
}
