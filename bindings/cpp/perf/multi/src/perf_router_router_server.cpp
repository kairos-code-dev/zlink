// ROUTER-ROUTER multi server benchmark: routed echo responder.
// Topology: client ROUTER(connect, N) <-> server ROUTER(bind, routing_id=SERVER)
// Measurement role: echo payload to sender routing id and emit queue metrics.

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"

#include <cerrno>
#include <chrono>
#include <deque>
#include <optional>
#include <vector>

namespace {

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

bool take_router_payload (std::vector<zlink::message_t> &parts,
                          zlink::message_t &payload)
{
    if (parts.empty ()) {
        payload = zlink::message_t (0);
        return payload.valid ();
    }

    if (parts.size () == 1) {
        payload = std::move (parts[0]);
        return true;
    }

    if (parts.size () == 2 && parts[0].size () == 0) {
        payload = std::move (parts[1]);
        return true;
    }

    errno = EPROTO;
    return false;
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
    if (!perf::multi::apply_benchmark_auto_hwm_msg_unit_typed (
          server, msg_size))
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

    const bench_multi_cpu_sample_t resource_probe_start =
      perf::multi::start_resource_probe ();
    perf::multi::print_ready (endpoint);

    struct pending_reply_t
    {
        zlink::routing_id_t rid;
        zlink::message_t payload;
    };

    bool stop_requested = false;
    bool failed = false;
    std::deque<pending_reply_t> pending_replies;
    zlink::poller_t poller;
    std::vector<zlink::poll_event_t> events;
    events.reserve (1);
    poller.add (server, zlink::poll_event_flag_t::pollin);

    auto flush_pending = [&] () -> bool {
        while (!pending_replies.empty ()) {
            pending_reply_t &front = pending_replies.front ();
            try {
                zlink::message_t attempt = front.payload;
                if (server.send (front.rid)
                      .message (attempt)
                      .flags (ZLINK_DONTWAIT)
                      .submit ()) {
                    pending_replies.pop_front ();
                    continue;
                }
                return true; // backpressured
            } catch (const zlink::submit_error_t &err) {
                const int err_no = err.internal_errno ();
                if (err_no == EINTR || err_no == EHOSTUNREACH
                    || err_no == ENOTCONN)
                    return true;
                return false;
            }
        }
        return true;
    };

    int poll_event_mask =
      static_cast<int> (zlink::poll_event_flag_t::pollin);
    while (!stop_requested) {
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
            poller.wait (events, 1, std::chrono::milliseconds (-1));
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

        // Drain available messages without blocking.
        while (true) {
            zlink::received_t received;
            const int recv_rc = server.recv (
              received, zlink::recv_flags_t::dontwait);
            if (recv_rc < 0) {
                const int err = errno;
                if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR)
                    break;
                debug_log ("recv failed errno=" + std::to_string (err));
                failed = true;
                break;
            }

            zlink::message_t moved_part;
            zlink::message_t *part = NULL;
            if (received.is_single_part ()) {
                part = &received.first_part ();
            } else {
                if (!take_router_payload (received.parts (), moved_part)) {
                    debug_log ("recv failed errno=" + std::to_string (errno));
                    failed = true;
                    break;
                }
                part = &moved_part;
            }
            if (perf::multi::is_stop_token (part->data (), part->size ())) {
                stop_requested = true;
                break;
            }
            if (part->size () == 0)
                continue;
            const std::optional<zlink::routing_id_t> &source_rid =
              received.routing_id ();
            if (!source_rid) {
                debug_log ("recv missing routing id");
                failed = true;
                break;
            }

            if (!pending_replies.empty ()) {
                pending_replies.push_back (pending_reply_t {
                  *source_rid, std::move (*part) });
                continue;
            }

            try {
                zlink::message_t attempt = *part;
                if (!server.send (*source_rid)
                       .message (attempt)
                       .flags (ZLINK_DONTWAIT)
                       .submit ()) {
                    pending_replies.push_back (pending_reply_t {
                      *source_rid, std::move (*part) });
                }
            } catch (const zlink::submit_error_t &err) {
                const int err_no = err.internal_errno ();
                if (err_no == EINTR || err_no == EHOSTUNREACH
                    || err_no == ENOTCONN) {
                    pending_replies.push_back (pending_reply_t {
                      *source_rid, std::move (*part) });
                    continue;
                }
                debug_log ("send failed errno=" + std::to_string (err_no));
                failed = true;
                break;
            }
        }
        if (failed)
            break;
    }

    const bench_multi_resource_metrics_t resource_metrics =
      perf::multi::finish_resource_probe (resource_probe_start);
    perf::multi::print_server_resource_metrics (
      lib_name,
      "MULTI_ROUTER_ROUTER",
      transport,
      msg_size,
      resource_metrics);
    perf::multi::print_server_queue_metrics (
      lib_name,
      "MULTI_ROUTER_ROUTER",
      transport,
      msg_size,
      perf::multi::server_queue_stats_t ());
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
