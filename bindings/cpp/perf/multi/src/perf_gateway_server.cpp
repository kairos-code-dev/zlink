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

    for (int i = 0; i < max_bind_tries; ++i) {
        const int base_port = settings.server_bind_port > 0
                                ? settings.server_bind_port
                                : base_seed + i * 8;
        reg_pub_endpoint = make_tcp_endpoint (base_port);
        reg_router_endpoint = make_tcp_endpoint (base_port + 1);
        provider_endpoint = make_transport_endpoint (transport, base_port + 2);

        std::unique_ptr<zlink::service::registry_t> reg (
          new zlink::service::registry_t (ctx.ctx ()));
        if (!reg->valid ())
            return;

        if (reg->set_endpoints (reg_pub_endpoint, reg_router_endpoint) != 0
            || reg->start () != 0) {
            continue;
        }

        std::unique_ptr<zlink::service::receiver_t> recv (
          new zlink::service::receiver_t (ctx.ctx ()));
        if (!recv->valid ())
            continue;

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

        if (!configure_receiver_tls (*recv, transport))
            return;

        if (recv->bind (provider_endpoint) != 0
            || recv->connect_registry (reg_router_endpoint) != 0
            || recv->register_service ("svc", provider_endpoint, 1) != 0) {
            continue;
        }

        registry = std::move (reg);
        receiver = std::move (recv);
        break;
    }

    if (!registry || !receiver)
        return;

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

    while (std::chrono::steady_clock::now () < deadline) {
        zlink::message_t client_id;
        const int id_rc = provider_router.recv (client_id, zlink::recv_flag::none);
        if (id_rc < 0) {
            const int err = errno;
            if (err == EAGAIN || err == EINTR)
                continue;
            break;
        }

        if (!client_id.more ())
            continue;

        zlink::message_t payload;
        if (provider_router.recv (payload, zlink::recv_flag::none) < 0) {
            const int err = errno;
            if (err == EAGAIN || err == EINTR)
                continue;
            break;
        }

        if (payload.more ())
            continue;

        if (perf::multi::is_stop_token (payload.data (), payload.size ()))
            break;

        if (provider_router.send (client_id.data (),
                                  client_id.size (),
                                  zlink::send_flag::sndmore)
            != static_cast<int> (client_id.size ())) {
            break;
        }

        if (provider_router.send (
              payload.data (), payload.size (), zlink::send_flag::none)
            != static_cast<int> (payload.size ())) {
            break;
        }
    }

    perf::multi::print_server_queue_metrics (
      "current",
      "MULTI_GATEWAY",
      transport,
      0,
      perf::multi::server_queue_stats_t ());
}
