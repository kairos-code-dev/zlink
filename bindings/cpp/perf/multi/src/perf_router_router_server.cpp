// ROUTER-ROUTER multi server benchmark: routed echo responder.
// Topology: client ROUTER(connect, N) <-> server ROUTER(bind, routing_id=SERVER)
// Measurement role: echo payload to sender routing id and emit queue metrics.

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"

#include <cerrno>
#include <chrono>
#include <vector>

namespace {

bool take_router_payload (std::vector<zlink::message_t> &parts,
                          zlink::message_t &payload)
{
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

struct reply_state_t
{
    bool pending;
    zlink_routing_id_t client_id;
    zlink::message_t payload;

    reply_state_t ()
        : pending (false),
          client_id (zlink::empty_routing_id ()),
          payload ()
    {
    }
};

long compute_wait_ms (const std::chrono::steady_clock::time_point &deadline)
{
    long wait_ms = 100;
    const long remain_ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                             deadline - std::chrono::steady_clock::now ())
                             .count ();
    if (remain_ms < wait_ms)
        wait_ms = remain_ms;
    if (wait_ms < 1)
        wait_ms = 1;
    return wait_ms;
}

bool try_send_reply (zlink::socket_t &sock,
                     zlink::poller_t &poller,
                     reply_state_t &reply)
{
    if (!reply.pending)
        return true;

    const int payload_sent =
      sock.send (reply.client_id, reply.payload, zlink::send_flag::dontwait);
    if (payload_sent != 0) {
        if (payload_sent < 0 && errno == EAGAIN) {
            try {
                poller.modify (
                  sock, zlink::poll_event::pollin | zlink::poll_event::pollout);
                return true;
            }
            catch (const zlink::zlink_error_t &) {
                return false;
            }
        }
        return false;
    }

    reply.pending = false;
    reply.client_id = zlink::empty_routing_id ();
    reply.payload = zlink::message_t ();
    try {
        poller.modify (sock, zlink::poll_event::pollin);
        return true;
    }
    catch (const zlink::zlink_error_t &) {
        return false;
    }
}

} // namespace

bool perf_router_router_server (const std::string &transport, size_t msg_size)
{
    perf::multi::set_perf_pattern_env ("ROUTER_ROUTER");

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED,MULTI_ROUTER_ROUTER," << transport << std::endl;
        return true;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();

    perf::multi::ctx_guard_t ctx;
    perf::multi::socket_guard_t server (ctx, zlink::socket_type::router);
    if (!server.valid ())
        return false;

    (void) server.sock ().set_routing_id (std::string ("SERVER"));
    perf::multi::apply_benchmark_socket_options (
      server.sock (), settings, transport);
    if (!perf::multi::setup_tls_server (server.sock (), transport))
        return false;

    const std::string endpoint = perf::multi::bind_and_resolve_endpoint (
      server.sock (), transport, "cpp_multi_router_router", settings.server_bind_port);
    if (endpoint.empty ())
        return false;

    const bench_multi_cpu_sample_t resource_probe_start =
      perf::multi::start_resource_probe ();
    perf::multi::print_ready (endpoint);

    const int active_seconds = settings.duration_seconds > 0 ? settings.duration_seconds : 1;
    const int deadline_seconds = active_seconds + 2;

    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::seconds (deadline_seconds);

    zlink::poller_t poller;
    (void) poller.add (server.sock (), zlink::poll_event::pollin, &server.sock ());
    std::vector<zlink::poll_event_t> events;
    events.reserve (1);
    reply_state_t reply;

    bool stop_requested = false;
    bool failed = false;
    while (!stop_requested && std::chrono::steady_clock::now () < deadline) {
        const int poll_rc =
          poller.wait_all (events, compute_wait_ms (deadline));
        if (poll_rc < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            failed = true;
            break;
        }
        if (poll_rc == 0)
            continue;

        for (size_t i = 0; i < events.size () && !stop_requested; ++i) {
            zlink::socket_t *sock =
              static_cast<zlink::socket_t *> (events[i].user);
            if (!sock)
                continue;

            if ((events[i].revents & static_cast<short> (zlink::poll_event::pollout))
                && reply.pending) {
                if (!try_send_reply (*sock, poller, reply)) {
                    stop_requested = true;
                    failed = true;
                    break;
                }
                if (reply.pending)
                    continue;
            }

            if (!(events[i].revents
                  & static_cast<short> (zlink::poll_event::pollin))) {
                continue;
            }

            for (;;) {
                if (reply.pending)
                    break;

                zlink::received_t received;
                if (sock->receive (received, zlink::recv_flag::dontwait) < 0) {
                    const int err = errno;
                    if (err == EAGAIN)
                        break;
                    if (err == EINTR)
                        continue;
                    stop_requested = true;
                    failed = true;
                    break;
                }

                zlink::message_t payload;
                if (!take_router_payload (received.parts, payload)) {
                    const int err = errno;
                    if (err == EPROTO)
                        continue;
                    stop_requested = true;
                    failed = true;
                    break;
                }

                if (perf::multi::is_stop_token (payload.data (), payload.size ())) {
                    stop_requested = true;
                    break;
                }
                if (payload.size () == 0)
                    continue;

                reply.pending = true;
                reply.client_id = received.routing_id;
                reply.payload = std::move (payload);
                if (!try_send_reply (*sock, poller, reply)) {
                    stop_requested = true;
                    failed = true;
                    break;
                }
                if (reply.pending)
                    break;
            }
        }
    }

    const bench_multi_resource_metrics_t resource_metrics =
      perf::multi::finish_resource_probe (resource_probe_start);
    perf::multi::print_server_resource_metrics (
      "current",
      "MULTI_ROUTER_ROUTER",
      transport,
      msg_size,
      resource_metrics);
    perf::multi::print_server_queue_metrics (
      "current",
      "MULTI_ROUTER_ROUTER",
      transport,
      msg_size,
      perf::multi::server_queue_stats_t ());
    return !failed;
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

    return perf_router_router_server (transport, size) ? 0 : 1;
}
