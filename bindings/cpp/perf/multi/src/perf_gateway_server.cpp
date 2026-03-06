// GATEWAY multi server benchmark: receiver service echo responder.
// Topology: client gateway_t(connect, N) <-> server receiver_t(bind, 1)
// Measurement role: route request payload back to client via receiver router.

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"

#include <cerrno>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <iostream>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

int bench_pid ()
{
#if defined(_WIN32)
    return _getpid ();
#else
    return getpid ();
#endif
}

std::string make_tcp_endpoint (int port)
{
    return std::string ("tcp://127.0.0.1:") + std::to_string (port);
}

std::string make_transport_endpoint (const std::string &transport, int port)
{
    const std::string suffix = std::to_string (port);
    if (transport == "ws")
        return std::string ("ws://127.0.0.1:") + suffix;
    if (transport == "wss")
        return std::string ("wss://127.0.0.1:") + suffix;
    if (transport == "tls")
        return std::string ("tls://127.0.0.1:") + suffix;
    return std::string ("tcp://127.0.0.1:") + suffix;
}

bool configure_receiver_tls (zlink::service::receiver_t &receiver,
                             const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    std::string cert;
    std::string key;
    std::string ca;
    if (!perf::multi::try_resolve_perf_tls_paths (cert, key, ca))
        return false;

    return receiver.set_tls_server (cert, key) == 0;
}

} // namespace

void perf_gateway_server (const std::string &transport, size_t)
{
    perf::multi::set_perf_pattern_env ("GATEWAY");

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED,MULTI_GATEWAY," << transport << std::endl;
        return;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();

    perf::multi::ctx_guard_t ctx;

    std::unique_ptr<zlink::service::registry_t> registry;
    std::unique_ptr<zlink::service::receiver_t> receiver;
    std::string reg_pub_endpoint;
    std::string reg_router_endpoint;
    std::string provider_endpoint;

    const int base_seed = settings.server_bind_port > 0
                            ? settings.server_bind_port
                            : 39000 + (bench_pid () % 1000) * 8;
    const int max_bind_tries = settings.server_bind_port > 0 ? 1 : 64;
    int last_errno = 0;
    const char *last_stage = "none";

    for (int i = 0; i < max_bind_tries; ++i) {
        const int base_port = settings.server_bind_port > 0
                                ? settings.server_bind_port
                                : base_seed + i * 8;
        reg_pub_endpoint = make_tcp_endpoint (base_port);
        reg_router_endpoint = make_tcp_endpoint (base_port + 1);
        provider_endpoint = make_transport_endpoint (transport, base_port + 2);

        std::unique_ptr<zlink::service::registry_t> reg (
          new zlink::service::registry_t (ctx.ctx ()));
        if (!reg->valid ()) {
            last_errno = reg->last_error ();
            last_stage = "registry_new";
            return;
        }

        if (reg->set_endpoints (reg_pub_endpoint, reg_router_endpoint) != 0
            || reg->start () != 0) {
            last_errno = errno;
            last_stage = "registry_start";
            continue;
        }
        perf::multi::settle ();

        std::unique_ptr<zlink::service::receiver_t> recv (
          new zlink::service::receiver_t (ctx.ctx ()));
        if (!recv->valid ()) {
            last_errno = recv->last_error ();
            last_stage = "receiver_new";
            continue;
        }

        (void) recv->set_sockopt (zlink::receiver_socket_role::router,
                                  zlink::socket_options::sndhwm,
                                  settings.sndhwm);
        (void) recv->set_sockopt (zlink::receiver_socket_role::router,
                                  zlink::socket_options::rcvhwm,
                                  settings.rcvhwm);
        (void) recv->set_sockopt (zlink::receiver_socket_role::router,
                                  zlink::socket_options::sndtimeo,
                                  settings.sndtimeo_ms);
        (void) recv->set_sockopt (zlink::receiver_socket_role::router,
                                  zlink::socket_options::rcvtimeo,
                                  settings.rcvtimeo_ms);

        if (!configure_receiver_tls (*recv, transport)) {
            last_errno = errno;
            last_stage = "receiver_tls";
            return;
        }

        if (recv->bind (provider_endpoint) != 0
            || recv->connect_registry (reg_router_endpoint) != 0
            || recv->register_service ("svc", provider_endpoint, 1) != 0) {
            last_errno = errno;
            last_stage = "receiver_bind_connect_register";
            continue;
        }

        registry = std::move (reg);
        receiver = std::move (recv);
        break;
    }

    if (!registry || !receiver) {
        std::cerr << "GATEWAY_SETUP_FAIL,stage=" << last_stage
                  << ",errno=" << last_errno
                  << ",pub=" << reg_pub_endpoint
                  << ",router=" << reg_router_endpoint
                  << ",provider=" << provider_endpoint << std::endl;
        return;
    }

    zlink::socket_t provider_router =
      zlink::socket_t::wrap (receiver->router_handle ());

    perf::multi::print_ready (reg_pub_endpoint);

    const int warmup_seconds = settings.warmup_seconds > 0 ? settings.warmup_seconds : 0;
    const int active_seconds = settings.duration_seconds > 0 ? settings.duration_seconds : 1;
    const int settle_seconds =
      settings.settle_ms > 0 ? (settings.settle_ms + 999) / 1000 : 0;
    const int deadline_seconds =
      warmup_seconds + settle_seconds + active_seconds + 2;

    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::seconds (deadline_seconds);

    zlink::poller_t poller;
    (void) poller.add (provider_router, zlink::poll_event::pollin);
    std::vector<zlink::poll_event_t> events;
    events.reserve (1);

    bool stop_requested = false;
    while (!stop_requested && std::chrono::steady_clock::now () < deadline) {
        const auto now = std::chrono::steady_clock::now ();
        long wait_ms = 100;
        const long remain_ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                                 deadline - now)
                                 .count ();
        if (remain_ms < wait_ms)
            wait_ms = remain_ms;
        if (wait_ms < 1)
            wait_ms = 1;

        const int poll_rc = poller.wait (events, wait_ms);
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

            for (;;) {
                zlink::message_t client_id;
                const int id_rc = sock->recv (client_id, zlink::recv_flag::dontwait);
                if (id_rc < 0) {
                    const int err = errno;
                    if (err == EAGAIN)
                        break;
                    if (err == EINTR)
                        continue;
                    stop_requested = true;
                    break;
                }

                if (!client_id.more ())
                    continue;

                zlink::message_t payload;
                const int payload_rc = sock->recv (payload, zlink::recv_flag::dontwait);
                if (payload_rc < 0) {
                    const int err = errno;
                    if (err == EAGAIN)
                        break;
                    if (err == EINTR)
                        continue;
                    stop_requested = true;
                    break;
                }

                if (payload.more ())
                    continue;

                if (perf::multi::is_stop_token (payload.data (), payload.size ())) {
                    stop_requested = true;
                    break;
                }

                if (sock->send (client_id.data (),
                                client_id.size (),
                                zlink::send_flag::sndmore)
                    != static_cast<int> (client_id.size ())) {
                    stop_requested = true;
                    break;
                }

                if (sock->send (
                      payload.data (), payload.size (), zlink::send_flag::none)
                    != static_cast<int> (payload.size ())) {
                    stop_requested = true;
                    break;
                }
            }
        }
    }

    perf::multi::print_server_queue_metrics (
      "current",
      "MULTI_GATEWAY",
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

    perf_gateway_server (transport, size);
    return 0;
}
