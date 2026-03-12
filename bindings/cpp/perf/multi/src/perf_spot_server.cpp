// SPOT multi server benchmark: one-way spot publisher source.
// Topology: server spot_node(pub bind, 1) -> client spot_node(sub connect, N)
// Measurement role: stamp payload phases and publish topic "bench".

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

static const char *k_service_name = "perf-spot";

int bench_pid ()
{
#if defined(_WIN32)
    return _getpid ();
#else
    return getpid ();
#endif
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

std::string make_tcp_endpoint (int port)
{
    return std::string ("tcp://127.0.0.1:") + std::to_string (port);
}

std::string bind_spot_endpoint (zlink::service::spot_node_t &node,
                                const std::string &transport,
                                int base_port)
{
    for (int i = 0; i < 64; ++i) {
        const std::string endpoint =
          make_transport_endpoint (transport, base_port + i);
        if (node.bind (endpoint) == 0)
            return endpoint;
    }
    return std::string ();
}

bool configure_spot_server_tls (zlink::service::spot_node_t &node,
                                const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    std::string cert;
    std::string key;
    std::string ca;
    if (!perf::multi::try_resolve_perf_tls_paths (cert, key, ca))
        return false;

    return node.set_tls_server (cert, key) == 0;
}

bool wait_pub_peer_ready (zlink::service::spot_node_t &node,
                          size_t target_count,
                          int timeout_ms)
{
    const size_t target = std::max<size_t> (1, target_count);
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (std::max (timeout_ms, 1000));
    while (std::chrono::steady_clock::now () < deadline) {
        size_t peer_count = 0;
        if (node.pub_peers (NULL, &peer_count) == 0 && peer_count >= target)
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    size_t peer_count = 0;
    return node.pub_peers (NULL, &peer_count) == 0 && peer_count >= target;
}

bool is_transient_publish_error (int err)
{
    return err == EINTR || err == EAGAIN || err == ENOTCONN || err == ENOENT
           || err == EHOSTUNREACH;
}

long compute_wait_ms (const perf::multi::multi_bench_settings_t &settings,
                      const std::chrono::steady_clock::time_point &deadline)
{
    long wait_ms = settings.client_poll_timeout_ms > 0 ? settings.client_poll_timeout_ms : 100;
    const long remain_ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                             deadline - std::chrono::steady_clock::now ())
                             .count ();
    if (remain_ms < wait_ms)
        wait_ms = remain_ms;
    if (wait_ms < 1)
        wait_ms = 1;
    return wait_ms;
}

bool run_phase (zlink::service::spot_t &spot,
                zlink::poller_t &poller,
                std::vector<zlink::poll_event_t> &events,
                std::vector<char> &payload,
                size_t msg_size,
                uint32_t run_id,
                uint64_t &seq,
                perf_metric::phase_t phase,
                std::chrono::steady_clock::duration duration,
                const perf::multi::multi_bench_settings_t &settings)
{
    if (duration <= std::chrono::steady_clock::duration::zero ())
        return true;

    bool pending = false;
    const char *topic = "bench";
    const auto deadline = std::chrono::steady_clock::now () + duration;
    while (std::chrono::steady_clock::now () < deadline) {
        if (!pending) {
            if (!perf_metric::stamp_payload (payload.data (),
                                             payload.size (),
                                             run_id,
                                             phase,
                                             msg_size,
                                             seq++,
                                             perf_metric::now_us ())) {
                return false;
            }
        }

        if (spot.publish (
              topic,
              payload.data (),
              payload.size (),
              zlink::send_flag::dontwait)
            == 0) {
            pending = false;
        } else if (is_transient_publish_error (errno)) {
            pending = true;
            if (poller.modify_spot_pub (spot, zlink::poll_event::pollout) != 0)
                return false;
        } else {
            return false;
        }

        if (!pending) {
            if (poller.modify_spot_pub (spot, static_cast<zlink::poll_event> (0))
                != 0)
                return false;
            continue;
        }

        const int poll_rc = poller.wait (events, compute_wait_ms (settings, deadline));
        if (poll_rc < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (poll_rc == 0)
            continue;
    }

    return true;
}

} // namespace

bool perf_spot_server (const std::string &transport, size_t msg_size)
{
    perf::multi::set_perf_pattern_env ("SPOT");

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED,MULTI_SPOT," << transport << std::endl;
        return true;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();
    const int send_timeout_ms = settings.sndtimeo_ms;
    const int xpub_nodrop =
      perf::multi::parse_positive_env ("PERF_MULTI_SPOT_XPUB_NODROP", 1) > 0 ? 1
                                                                              : 0;

    perf::multi::ctx_guard_t ctx;
    zlink::service::registry_t registry (ctx.ctx ());
    zlink::service::discovery_t discovery (ctx.ctx (), zlink::service_type::spot);
    zlink::service::spot_node_t node (ctx.ctx ());

    (void) node.set_sockopt (
      zlink::spot_node_socket_role::pub,
      zlink::socket_options::sndhwm,
      settings.sndhwm);
    (void) node.set_sockopt (
      zlink::spot_node_socket_role::pub,
      zlink::socket_options::sndtimeo,
      send_timeout_ms);
    (void) node.set_sockopt (
      zlink::spot_node_socket_role::pub,
      zlink::socket_options::xpub_nodrop,
      xpub_nodrop);

    const int registry_base_port = settings.server_bind_port > 0
                                     ? settings.server_bind_port + 64
                                     : 30000 + ((bench_pid () % 5000) * 3);
    const std::string registry_pub_endpoint = make_tcp_endpoint (registry_base_port + 1);
    const std::string registry_router_endpoint =
      make_tcp_endpoint (registry_base_port + 2);
    if (!registry.valid () || !discovery.valid ()
        || registry.set_endpoints (registry_pub_endpoint, registry_router_endpoint)
             != 0
        || registry.set_broadcast_interval (
             static_cast<uint32_t> (std::max (100, settings.settle_ms)))
             != 0
        || registry.start () != 0
        || discovery.connect_registry (registry_router_endpoint) != 0
        || node.set_discovery (discovery.handle (), k_service_name) != 0) {
        return false;
    }

    if (!configure_spot_server_tls (node, transport))
        return false;

    const int base_port = settings.server_bind_port > 0
                            ? settings.server_bind_port
                            : 39500 + (bench_pid () % 1000) * 8;
    const std::string endpoint = bind_spot_endpoint (node, transport, base_port);
    if (endpoint.empty ())
        return false;
    if (node.register_service (k_service_name, endpoint) != 0)
        return false;

    zlink::service::spot_t spot (node);
    if (!spot.valid ())
        return false;

    perf::multi::print_ready (
      endpoint + "|" + registry_pub_endpoint + "|" + registry_router_endpoint);

    const size_t service_clients = std::max<size_t> (1, settings.clients);
    if (!wait_pub_peer_ready (node, service_clients, settings.connect_ready_timeout_ms)
        && std::getenv ("PERF_DEBUG") != NULL) {
        std::cerr << "spot server: peer wait timed out, continuing" << std::endl;
    }
    if (std::getenv ("PERF_DEBUG") != NULL) {
        size_t peer_count = 0;
        if (node.pub_peers (NULL, &peer_count) == 0)
            std::cerr << "spot server: pub peers=" << peer_count << std::endl;
    }

    std::vector<char> payload (
      std::max<size_t> (msg_size, perf_metric::header_size ()), 's');

    const uint32_t run_id = 1;
    uint64_t seq = 1;

    zlink::poller_t poller;
    std::vector<zlink::poll_event_t> events;
    events.reserve (1);
    if (poller.add_spot_pub (spot, zlink::poll_event::pollout) != 0) {
        if (std::getenv ("PERF_DEBUG") != NULL)
            std::cerr << "spot server: add poller failed errno=" << errno
                      << std::endl;
        return false;
    }
    if (poller.modify_spot_pub (spot, static_cast<zlink::poll_event> (0)) != 0) {
        if (std::getenv ("PERF_DEBUG") != NULL)
            std::cerr << "spot server: modify poller failed errno=" << errno
                      << std::endl;
        return false;
    }

    if (!run_phase (spot,
                    poller,
                    events,
                    payload,
                    msg_size,
                    run_id,
                    seq,
                    perf_metric::phase_warmup,
                    std::chrono::seconds (std::max (0, settings.warmup_seconds)),
                    settings)) {
        if (std::getenv ("PERF_DEBUG") != NULL)
            std::cerr << "spot server: warmup failed errno=" << errno << std::endl;
        return false;
    }
    if (!run_phase (spot,
                    poller,
                    events,
                    payload,
                    msg_size,
                    run_id,
                    seq,
                    perf_metric::phase_drain,
                    std::chrono::milliseconds (std::max (0, settings.settle_ms)),
                    settings)) {
        if (std::getenv ("PERF_DEBUG") != NULL)
            std::cerr << "spot server: settle failed errno=" << errno << std::endl;
        return false;
    }
    if (!run_phase (spot,
                    poller,
                    events,
                    payload,
                    msg_size,
                    run_id,
                    seq,
                    perf_metric::phase_active,
                    std::chrono::seconds (std::max (1, settings.duration_seconds)),
                    settings)) {
        if (std::getenv ("PERF_DEBUG") != NULL)
            std::cerr << "spot server: active failed errno=" << errno << std::endl;
        return false;
    }

    perf::multi::print_server_queue_metrics (
      "current",
      "MULTI_SPOT",
      transport,
      msg_size,
      perf::multi::server_queue_stats_t ());
    return true;
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

    return perf_spot_server (transport, size) ? 0 : 1;
}
