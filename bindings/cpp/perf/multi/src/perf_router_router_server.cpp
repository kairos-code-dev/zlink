// ROUTER-ROUTER multi server benchmark: routed echo responder.
// Topology: client ROUTER(connect, N) <-> server ROUTER(bind, routing_id=SERVER)
// Measurement role: echo payload to sender routing id and emit queue metrics.

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"

#include <cerrno>
#include <chrono>
#include <vector>

namespace {

struct reply_state_t
{
    bool pending;
    bool id_sent;
    zlink::message_t client_id;
    zlink::message_t payload;

    reply_state_t () : pending (false), id_sent (false), client_id (), payload () {}
};

bool recv_router_request (zlink::socket_t &sock,
                          zlink::message_t &client_id,
                          zlink::message_t &payload)
{
    const int id_rc = sock.recv (client_id, zlink::recv_flag::dontwait);
    if (id_rc < 0)
        return false;
    if (!client_id.more ()) {
        errno = EPROTO;
        return false;
    }

    zlink::message_t payload_or_delim;
    if (sock.recv (payload_or_delim, zlink::recv_flag::none) < 0)
        return false;

    if (payload_or_delim.more ()) {
        if (payload_or_delim.size () != 0) {
            errno = EPROTO;
            return false;
        }
        if (sock.recv (payload, zlink::recv_flag::none) < 0)
            return false;
    } else {
        payload = std::move (payload_or_delim);
    }

    if (payload.more ()) {
        errno = EPROTO;
        return false;
    }
    return true;
}

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

    if (!reply.id_sent) {
        const int id_sent = sock.send (reply.client_id.data (),
                                       reply.client_id.size (),
                                       zlink::send_flag::sndmore
                                         | zlink::send_flag::dontwait);
        if (id_sent != static_cast<int> (reply.client_id.size ())) {
            if (id_sent < 0 && errno == EAGAIN) {
                return poller.modify (
                         sock,
                         zlink::poll_event::pollin | zlink::poll_event::pollout)
                       == 0;
            }
            return false;
        }
        reply.id_sent = true;
    }

    const int payload_sent = sock.send (reply.payload.data (),
                                        reply.payload.size (),
                                        zlink::send_flag::dontwait);
    if (payload_sent != static_cast<int> (reply.payload.size ())) {
        if (payload_sent < 0 && errno == EAGAIN) {
            return poller.modify (
                     sock,
                     zlink::poll_event::pollin | zlink::poll_event::pollout)
                   == 0;
        }
        return false;
    }

    reply.pending = false;
    reply.id_sent = false;
    reply.client_id = zlink::message_t ();
    reply.payload = zlink::message_t ();
    return poller.modify (sock, zlink::poll_event::pollin) == 0;
}

} // namespace

void perf_router_router_server (const std::string &transport, size_t)
{
    perf::multi::set_perf_pattern_env ("ROUTER_ROUTER");

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED,MULTI_ROUTER_ROUTER," << transport << std::endl;
        return;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();

    perf::multi::ctx_guard_t ctx;
    perf::multi::socket_guard_t server (ctx, zlink::socket_type::router);
    if (!server.valid ())
        return;

    (void) server.sock ().set (zlink::socket_options::routing_id,
                               std::string ("SERVER"));
    perf::multi::apply_benchmark_socket_options (
      server.sock (), settings, transport);
    if (!perf::multi::setup_tls_server (server.sock (), transport))
        return;

    const std::string endpoint = perf::multi::bind_and_resolve_endpoint (
      server.sock (), transport, "cpp_multi_router_router", settings.server_bind_port);
    if (endpoint.empty ())
        return;

    perf::multi::print_ready (endpoint);

    const int warmup_seconds = settings.warmup_seconds > 0 ? settings.warmup_seconds : 0;
    const int active_seconds = settings.duration_seconds > 0 ? settings.duration_seconds : 1;
    const int settle_seconds =
      settings.settle_ms > 0 ? (settings.settle_ms + 999) / 1000 : 0;
    const int deadline_seconds =
      warmup_seconds + settle_seconds + active_seconds + 2;

    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::seconds (deadline_seconds);

    zlink::poller_t poller;
    (void) poller.add (server.sock (), zlink::poll_event::pollin);
    std::vector<zlink::poll_event_t> events;
    events.reserve (1);
    reply_state_t reply;

    bool stop_requested = false;
    while (!stop_requested && std::chrono::steady_clock::now () < deadline) {
        const int poll_rc = poller.wait (events, compute_wait_ms (deadline));
        if (poll_rc < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (poll_rc == 0)
            continue;

        for (size_t i = 0; i < events.size () && !stop_requested; ++i) {
            zlink::socket_t *sock = events[i].socket;
            if (!sock)
                continue;

            if ((events[i].revents & static_cast<short> (zlink::poll_event::pollout))
                && reply.pending) {
                if (!try_send_reply (*sock, poller, reply)) {
                    stop_requested = true;
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

                zlink::message_t client_id;
                zlink::message_t payload;
                if (!recv_router_request (*sock, client_id, payload)) {
                    const int err = errno;
                    if (err == EAGAIN)
                        break;
                    if (err == EINTR)
                        continue;
                    stop_requested = true;
                    break;
                }

                if (perf::multi::is_stop_token (payload.data (), payload.size ())) {
                    stop_requested = true;
                    break;
                }
                if (payload.size () == 0)
                    continue;

                reply.pending = true;
                reply.id_sent = false;
                reply.client_id = std::move (client_id);
                reply.payload = std::move (payload);
                if (!try_send_reply (*sock, poller, reply)) {
                    stop_requested = true;
                    break;
                }
                if (reply.pending)
                    break;
            }
        }
    }

    perf::multi::print_server_queue_metrics (
      "current",
      "MULTI_ROUTER_ROUTER",
      transport,
      0,
      perf::multi::server_queue_stats_t ());
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

    perf_router_router_server (transport, size);
    return 0;
}
