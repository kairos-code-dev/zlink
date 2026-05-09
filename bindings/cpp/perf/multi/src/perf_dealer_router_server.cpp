// DEALER-ROUTER multi server benchmark: echo responder.
// Topology: client DEALER(connect, N) <-> server ROUTER(bind, 1)
// Measurement role: receive request payload and echo same payload back.

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <deque>
#include <vector>

namespace {

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
    perf::multi::socket_guard_t server (ctx, zlink::socket_type::router);
    if (!server.valid ())
        return false;

    perf::multi::apply_benchmark_socket_options (
      server.sock (), settings, transport);
    if (!perf::multi::setup_tls_server (server.sock (), transport))
        return false;

    const std::string endpoint = perf::multi::bind_and_resolve_endpoint (
      server.sock (), transport, "cpp_multi_dealer_router", settings.server_bind_port);
    if (endpoint.empty ())
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
    zlink::poller_t poller;
    std::vector<zlink::poll_event_t> events;
    events.reserve (1);
    server.sock ().poller_add (poller, zlink::poll_event_flag_t::pollin);

    auto flush_pending = [&] () -> bool {
        while (!pending_replies.empty ()) {
            pending_reply_t &front = pending_replies.front ();
            zlink::send_result_t result = zlink::send_result_t::sent;
            const int rc = server.sock ().send_no_wait_result (
              result, front.rid, front.payload);
            if (rc != 0) {
                const int err = errno;
                if (err == EINTR || err == EHOSTUNREACH || err == ENOTCONN)
                    return true;
                return false;
            }
            if (result == zlink::send_result_t::sent) {
                pending_replies.pop_front ();
                continue;
            }
            return true; // backpressured
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
        server.sock ().poller_modify (poller, mask);

        try {
            events =
              poller.wait_all (1, std::chrono::milliseconds (-1));
        }
        catch (const zlink::zlink_error_t &err) {
            const int err_no = err.internal_errno ();
            if (err_no == EINTR)
                continue;
            failed = true;
            break;
        }
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

        // Drain available messages without blocking.
        while (true) {
            zlink::received_t received;
            const int recv_rc = server.sock ().receive (
              received, ZLINK_DONTWAIT);
            if (recv_rc < 0) {
                const int err = errno;
                if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR)
                    break;
                failed = true;
                break;
            }
            if (!received.routing_id ().has_value ()) {
                failed = true;
                break;
            }
            zlink::routing_id_t source_rid = *received.routing_id ();
            zlink::message_t part;
            if (!take_router_payload (received.parts (), part)) {
                failed = true;
                break;
            }

            if (perf::multi::is_stop_token (part.data (), part.size ())) {
                stop_requested = true;
                break;
            }
            if (part.size () == 0)
                continue;
            if (!pending_replies.empty ()) {
                pending_replies.push_back (pending_reply_t {
                  std::move (source_rid), std::move (part) });
                continue;
            }
            zlink::send_result_t result = zlink::send_result_t::sent;
            const int rc = server.sock ().send_no_wait_result (
              result, source_rid, part);
            if (rc != 0) {
                const int err = errno;
                if (err == EINTR || err == EHOSTUNREACH || err == ENOTCONN) {
                    pending_replies.push_back (pending_reply_t {
                      std::move (source_rid), std::move (part) });
                    continue;
                }
                failed = true;
                break;
            }
            if (result != zlink::send_result_t::sent) {
                pending_replies.push_back (pending_reply_t {
                  std::move (source_rid), std::move (part) });
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
