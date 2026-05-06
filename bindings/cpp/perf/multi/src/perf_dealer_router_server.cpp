// DEALER-ROUTER multi server benchmark: echo responder.
// Topology: client DEALER(connect, N) <-> server ROUTER(bind, 1)
// Measurement role: receive request payload and echo same payload back.

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"

#include <cerrno>
#include <cstring>
#include <deque>

namespace {

zlink::routing_id_t routing_id_from_ascii (const char *value_)
{
    return zlink::routing_id_t::from_bytes (
      reinterpret_cast<const uint8_t *> (value_), std::strlen (value_));
}

struct pending_reply_t
{
    pending_reply_t () : client_id (routing_id_from_ascii ("x")), payload () {}

    zlink::routing_id_t client_id;
    zlink::message_t payload;
};

enum send_status_t
{
    send_status_sent = 0,
    send_status_blocked = 1,
    send_status_failed = 2
};

send_status_t try_send_reply (::perf::socket_t &sock, pending_reply_t &reply)
{
    zlink::send_result_t send_result = zlink::send_result_t::sent;
    const int rc =
      sock.send_no_wait_result (send_result, reply.client_id, reply.payload);
    if (rc != 0)
        return send_status_failed;
    if (send_result == zlink::send_result_t::sent)
        return send_status_sent;
    if (send_result == zlink::send_result_t::backpressured
        || send_result == zlink::send_result_t::not_ready) {
        return send_status_blocked;
    }
    errno = EIO;
    return send_status_failed;
}

bool flush_pending_replies (::perf::socket_t &sock,
                            std::deque<pending_reply_t> &pending_replies)
{
    while (!pending_replies.empty ()) {
        pending_reply_t &reply = pending_replies.front ();
        const send_status_t status = try_send_reply (sock, reply);
        if (status == send_status_sent) {
            pending_replies.pop_front ();
            continue;
        }
        if (status == send_status_blocked)
            return true;
        return false;
    }

    return true;
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

    zlink::poller_t poller;
    try {
        poller.add (server.sock (), zlink::poll_event::pollin);
    }
    catch (const zlink::zlink_error_t &) {
        return false;
    }

    std::vector<zlink::poll_event_t> events;
    events.reserve (1);
    std::deque<pending_reply_t> pending_replies;
    bool stop_requested = false;
    bool failed = false;
    while (!stop_requested) {
        const zlink::poll_event wait_events =
          pending_replies.empty ()
            ? zlink::poll_event::pollin
            : (zlink::poll_event::pollin | zlink::poll_event::pollout);
        try {
            poller.modify (server.sock (), wait_events);
        }
        catch (const zlink::zlink_error_t &) {
            failed = true;
            break;
        }

        events = poller.wait_all (50);
        const int poll_rc = static_cast<int> (events.size ());
        if (poll_rc < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            failed = true;
            break;
        }
        if (poll_rc == 0)
            continue;

        for (size_t i = 0; i < events.size () && !stop_requested; ++i) {
            if ((static_cast<short> (events[i].revents) & static_cast<short> (zlink::poll_event::pollout))
                != 0
                && !flush_pending_replies (server.sock (), pending_replies)) {
                failed = true;
                break;
            }

            if ((static_cast<short> (events[i].revents) & static_cast<short> (zlink::poll_event::pollin))
                == 0) {
                continue;
            }

            for (;;) {
                zlink::routing_id_t source_rid = routing_id_from_ascii ("x");
                zlink::message_t part;
                const int recv_rc = server.sock ().recv (
                  source_rid, part, zlink::recv_flags_t::dontwait);
                if (recv_rc < 0) {
                    const int err = errno;
                    if (err == EINTR)
                        continue;
                    if (err == EAGAIN)
                        break;
                    failed = true;
                    stop_requested = true;
                    break;
                }

                if (perf::multi::is_stop_token (part.data (), part.size ())) {
                    stop_requested = true;
                    break;
                }

                pending_reply_t reply;
                reply.client_id = source_rid;
                if (!part.valid ()) {
                    reply.payload = zlink::message_t (0);
                    if (!reply.payload.valid ()) {
                        failed = true;
                        stop_requested = true;
                        break;
                    }
                } else {
                    reply.payload = std::move (part);
                }

                if (!pending_replies.empty ()) {
                    pending_replies.push_back (std::move (reply));
                    continue;
                }

                const send_status_t status =
                  try_send_reply (server.sock (), reply);
                if (status == send_status_sent)
                    continue;
                if (status == send_status_blocked) {
                    pending_replies.push_back (std::move (reply));
                    continue;
                }
                failed = true;
                stop_requested = true;
                break;
            }
        }
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
