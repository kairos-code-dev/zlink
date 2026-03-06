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

bool send_spot_payload (zlink::socket_t &publisher,
                        const char *topic,
                        const void *payload,
                        size_t payload_size,
                        zlink::send_flag flags,
                        bool *topic_sent)
{
    if (!topic || !topic_sent)
        return false;

    const size_t topic_size = std::strlen (topic);
    if (!*topic_sent) {
        const int topic_rc =
          publisher.send (topic, topic_size, flags | zlink::send_flag::sndmore);
        if (topic_rc != static_cast<int> (topic_size))
            return false;
        *topic_sent = true;
    }

    return publisher.send (payload, payload_size, flags)
           == static_cast<int> (payload_size);
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

bool run_phase (zlink::socket_t &publisher,
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
    bool topic_sent = false;
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

        if (send_spot_payload (publisher,
                               "bench",
                               payload.data (),
                               payload.size (),
                               zlink::send_flag::dontwait,
                               &topic_sent)) {
            pending = false;
            topic_sent = false;
            if (poller.modify (publisher, static_cast<zlink::poll_event> (0)) != 0)
                return false;
            continue;
        }

        if (errno == EAGAIN) {
            pending = true;
            if (poller.modify (publisher, zlink::poll_event::pollout) != 0)
                return false;
        } else {
            return false;
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

    zlink::socket_t pub_socket = zlink::socket_t::wrap (node.pub_socket_handle ());
    if (!pub_socket.handle ())
        return;

    perf::multi::print_ready (endpoint);

    if (!wait_pub_peer_ready (node, settings.connect_ready_timeout_ms))
        return;

    std::vector<char> payload (
      std::max<size_t> (msg_size, perf_metric::header_size ()), 's');

    const uint32_t run_id = 1;
    uint64_t seq = 1;

    zlink::poller_t poller;
    std::vector<zlink::poll_event_t> events;
    events.reserve (1);
    (void) poller.add (pub_socket, zlink::poll_event::pollout);
    (void) poller.modify (pub_socket, static_cast<zlink::poll_event> (0));

    if (!run_phase (pub_socket,
                    poller,
                    events,
                    payload,
                    msg_size,
                    run_id,
                    seq,
                    perf_metric::phase_warmup,
                    std::chrono::seconds (std::max (0, settings.warmup_seconds)),
                    settings))
        return;
    if (!run_phase (pub_socket,
                    poller,
                    events,
                    payload,
                    msg_size,
                    run_id,
                    seq,
                    perf_metric::phase_drain,
                    std::chrono::milliseconds (std::max (0, settings.settle_ms)),
                    settings))
        return;
    if (!run_phase (pub_socket,
                    poller,
                    events,
                    payload,
                    msg_size,
                    run_id,
                    seq,
                    perf_metric::phase_active,
                    std::chrono::seconds (std::max (1, settings.duration_seconds)),
                    settings))
        return;

    perf::multi::print_server_queue_metrics (
      "current",
      "MULTI_SPOT",
      transport,
      msg_size,
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

    perf_spot_server (transport, size);
    return 0;
}
