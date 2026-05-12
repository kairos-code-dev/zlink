// DEALER-ROUTER multi server benchmark: echo responder.
// Topology: client DEALER(connect, N) <-> server ROUTER(bind, 1)
// Measurement role: receive request payload and echo same payload back.

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"

#include <cerrno>
#include <chrono>
#include <deque>
#include <optional>
#include <vector>

bool perf_dealer_router_server (const std::string &lib_name,
                                const std::string &transport,
                                size_t msg_size)
{
    perf::multi::set_perf_pattern_env ("DEALER_ROUTER");

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << ",MULTI_DEALER_ROUTER,"
                  << transport
                  << std::endl;
        return true;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();

    perf::multi::ctx_guard_t ctx;
    zlink::router_socket_t server (ctx.ctx ());
    if (!server.valid ())
        return false;

    perf::multi::apply_benchmark_socket_options (
      server, settings, transport);
    if (!perf::multi::apply_benchmark_auto_hwm_msg_unit_typed (
          server, msg_size))
        return false;
    if (!perf::multi::setup_tls_server (server, transport))
        return false;

    const std::string endpoint = perf::multi::bind_and_resolve_endpoint (
      server, transport, "cpp_multi_dealer_router", settings.server_bind_port);
    if (endpoint.empty ())
        return false;
    if (!perf::multi::recalculate_auto_hwm (ctx))
        return false;

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
    int poll_event_mask =
      static_cast<int> (zlink::poll_event_flag_t::pollin);
    zlink::poller_t poller;
    std::vector<zlink::poll_event_t> events;
    events.reserve (1);
    poller.add (server, zlink::poll_event_flag_t::pollin);

    auto flush_pending = [&] () -> bool {
        while (!pending_replies.empty ()) {
            pending_reply_t &front = pending_replies.front ();
            try {
                if (server.send (
                      front.rid, front.payload, zlink::send_flags_t::dontwait)) {
                    pending_replies.pop_front ();
                    continue;
                }
                return true; // backpressured
            }
            catch (const zlink::submit_error_t &err) {
                const int err_no = err.internal_errno ();
                if (err_no == EINTR || err_no == EHOSTUNREACH
                    || err_no == ENOTCONN)
                    return true;
                return false;
            }
        }
        return true;
    };

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
            if (events.empty ())
                continue;
            const auto revents_value = static_cast<int> (events[0].revents);
            const bool readable =
              (revents_value
               & static_cast<int> (zlink::poll_event_flag_t::pollin))
              != 0;
            const bool writable =
              (revents_value
               & static_cast<int> (zlink::poll_event_flag_t::pollout))
              != 0;

            if (writable && !pending_replies.empty ()) {
                if (!flush_pending ()) {
                    failed = true;
                    break;
                }
            }
            if (!readable)
                continue;
        }
        catch (const zlink::zlink_error_t &err) {
            const int err_no = err.internal_errno ();
            if (err_no == EINTR)
                continue;
            failed = true;
            break;
        }

        // Drain available messages without blocking.
        while (true) {
            zlink::received_t received;
            const int recv_rc = server.recv (
              received, zlink::recv_flags_t::dontwait);
            if (recv_rc < 0) {
                const int err = errno;
                if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR)
                    break;
                failed = true;
                break;
            }

            if (!received.is_single_part ())
                continue;
            zlink::message_t &part = received.first_part ();
            if (perf::multi::is_stop_token (part.data (), part.size ())) {
                stop_requested = true;
                break;
            }
            if (part.size () == 0)
                continue;
            const std::optional<zlink::routing_id_t> &source_rid =
              received.routing_id ();
            if (!source_rid) {
                failed = true;
                break;
            }
            if (!pending_replies.empty ()) {
                pending_replies.push_back (pending_reply_t {
                  *source_rid, std::move (part) });
                continue;
            }
            try {
                if (!received.send (part, zlink::send_flags_t::dontwait)) {
                    pending_replies.push_back (pending_reply_t {
                      *source_rid, std::move (part) });
                }
            }
            catch (const zlink::submit_error_t &err) {
                const int err_no = err.internal_errno ();
                if (err_no == EINTR || err_no == EHOSTUNREACH
                    || err_no == ENOTCONN) {
                    pending_replies.push_back (pending_reply_t {
                      *source_rid, std::move (part) });
                    continue;
                }
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
      "MULTI_DEALER_ROUTER",
      transport,
      msg_size,
      resource_metrics);
    perf::multi::print_server_queue_metrics (
      lib_name,
      "MULTI_DEALER_ROUTER",
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

    return perf_dealer_router_server (lib_name, transport, size) ? 0 : 1;
}
