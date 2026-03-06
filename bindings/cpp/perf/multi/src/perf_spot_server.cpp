// SPOT multi server benchmark: one-way spot publisher source.
// Topology: server spot_node(pub bind, 1) -> client spot_node(sub connect, N)
// Measurement role: stamp payload phases and publish topic "bench".

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <vector>

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
                          int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (std::max (timeout_ms, 1000));
    while (std::chrono::steady_clock::now () < deadline) {
        size_t peer_count = 0;
        if (node.pub_peers (NULL, &peer_count) == 0 && peer_count > 0)
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }

    size_t peer_count = 0;
    return node.pub_peers (NULL, &peer_count) == 0 && peer_count > 0;
}

bool send_spot_payload (zlink::service::spot_t &publisher,
                        const char *topic,
                        const void *payload,
                        size_t payload_size)
{
    return topic
           && publisher.publish (
                topic, payload, payload_size, zlink::send_flag::none)
                == 0;
}

} // namespace

void perf_spot_server (const std::string &transport, size_t msg_size)
{
    perf::multi::set_perf_pattern_env ("SPOT");

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED,MULTI_SPOT," << transport << std::endl;
        return;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();
    const int send_timeout_ms = settings.sndtimeo_ms;

    perf::multi::ctx_guard_t ctx;
    zlink::service::spot_node_t node (ctx.ctx ());

    (void) node.set_sockopt (
      zlink::spot_node_socket_role::pub,
      zlink::socket_options::sndhwm,
      settings.sndhwm);
    (void) node.set_sockopt (
      zlink::spot_node_socket_role::pub,
      zlink::socket_options::rcvhwm,
      settings.rcvhwm);
    (void) node.set_sockopt (
      zlink::spot_node_socket_role::pub,
      zlink::socket_options::sndtimeo,
      send_timeout_ms);
    (void) node.set_sockopt (
      zlink::spot_node_socket_role::pub,
      zlink::socket_options::rcvtimeo,
      settings.rcvtimeo_ms);

    if (!configure_spot_server_tls (node, transport))
        return;

    const int base_port = settings.server_bind_port > 0
                            ? settings.server_bind_port
                            : 39500 + (bench_pid () % 1000) * 8;
    const std::string endpoint = bind_spot_endpoint (node, transport, base_port);
    if (endpoint.empty ())
        return;

    zlink::service::spot_t publisher (node);
    if (!publisher.valid ())
        return;

    perf::multi::print_ready (endpoint);

    if (!wait_pub_peer_ready (node, settings.connect_ready_timeout_ms))
        return;

    std::vector<char> payload (
      std::max<size_t> (msg_size, perf_metric::header_size ()), 's');

    const uint32_t run_id = 1;
    uint64_t seq = 1;

    auto run_phase = [&] (perf_metric::phase_t phase,
                          std::chrono::milliseconds duration) -> bool {
        if (duration.count () <= 0)
            return true;

        const auto deadline = std::chrono::steady_clock::now () + duration;
        while (std::chrono::steady_clock::now () < deadline) {
            (void) perf_metric::stamp_payload (payload.data (),
                                               payload.size (),
                                               run_id,
                                               phase,
                                               msg_size,
                                               seq++,
                                               perf_metric::now_us ());
            if (!send_spot_payload (
                  publisher, "bench", payload.data (), payload.size ())) {
                const int err = errno;
                if (err == EAGAIN || err == EINTR)
                    continue;
                return false;
            }
        }
        return true;
    };

    if (!run_phase (perf_metric::phase_warmup,
                    std::chrono::seconds (std::max (0, settings.warmup_seconds))))
        return;
    if (!run_phase (
          perf_metric::phase_drain,
          std::chrono::milliseconds (std::max (0, settings.settle_ms))))
        return;
    if (!run_phase (perf_metric::phase_active,
                    std::chrono::seconds (std::max (1, settings.duration_seconds))))
        return;

    perf::multi::print_server_queue_metrics (
      "current",
      "MULTI_SPOT",
      transport,
      msg_size,
      perf::multi::server_queue_stats_t ());

    std::exit (0);
}
