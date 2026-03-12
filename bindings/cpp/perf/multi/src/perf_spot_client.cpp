// SPOT multi client benchmark: one-way spot subscriber receive workload.
// Topology: server spot_node(pub bind, 1) -> client spot_node(sub connect, N)
// Measurement: active-phase receive throughput + header-based latency sample.

#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_entry.hpp"
#include "../common/perf_client_helpers.hpp"
#include "../common/perf_metric_header.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

static const char *k_pattern_env = "SPOT";
static const char *k_pattern_result = "MULTI_SPOT";
static const char *k_service_name = "perf-spot";
static const uint32_t k_run_id = 1;
static const int k_active_search_extension_seconds = 2;
static const char *k_topic = "bench";

bool perf_debug_enabled ()
{
    return std::getenv ("PERF_DEBUG") != NULL;
}

void perf_debug_recv_once (const std::string &message)
{
    if (!perf_debug_enabled ())
        return;
    static bool emitted = false;
    if (emitted)
        return;
    emitted = true;
    std::cerr << message << std::endl;
}

bool parse_ready_payload (const std::string &raw,
                          std::string *server_endpoint_out,
                          std::string *registry_pub_out,
                          std::string *registry_router_out)
{
    if (!server_endpoint_out || !registry_pub_out || !registry_router_out)
        return false;

    server_endpoint_out->clear ();
    registry_pub_out->clear ();
    registry_router_out->clear ();

    const size_t first = raw.find ('|');
    if (first == std::string::npos)
        return false;
    const size_t second = raw.find ('|', first + 1);
    if (second == std::string::npos)
        return false;

    *server_endpoint_out = raw.substr (0, first);
    *registry_pub_out = raw.substr (first + 1, second - first - 1);
    *registry_router_out = raw.substr (second + 1);
    return !server_endpoint_out->empty () && !registry_pub_out->empty ()
           && !registry_router_out->empty ();
}

bool configure_spot_client_tls (zlink::service::spot_node_t &node,
                                const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    std::string cert;
    std::string key;
    std::string ca;
    if (!perf::multi::try_resolve_perf_tls_paths (cert, key, ca))
        return false;

    return node.set_tls_client (ca, "localhost", 0) == 0;
}

bool wait_sub_peer_ready (zlink::service::spot_node_t &node, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (std::max (timeout_ms, 1000));
    while (std::chrono::steady_clock::now () < deadline) {
        size_t peer_count = 0;
        if (node.sub_peers (NULL, &peer_count) == 0 && peer_count > 0)
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    size_t peer_count = 0;
    return node.sub_peers (NULL, &peer_count) == 0 && peer_count > 0;
}

bool wait_all_sub_peers (
  const std::vector<std::unique_ptr<zlink::service::spot_node_t> > &nodes,
  int timeout_ms)
{
    if (nodes.empty ())
        return false;

    std::vector<unsigned char> ready (nodes.size (), 0);
    size_t ready_count = 0;
    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (std::max (timeout_ms, 1000));

    while (std::chrono::steady_clock::now () < deadline && ready_count < nodes.size ()) {
        for (size_t i = 0; i < nodes.size (); ++i) {
            if (ready[i] || !nodes[i].get ())
                continue;
            size_t peer_count = 0;
            if (nodes[i]->sub_peers (NULL, &peer_count) == 0 && peer_count > 0) {
                ready[i] = 1;
                ++ready_count;
            }
        }
        if (ready_count >= nodes.size ())
            break;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    return ready_count == nodes.size ();
}

struct phase_config_t
{
    int warmup_seconds;
    int settle_ms;
    int active_seconds;
};

struct bench_result_t
{
    unsigned long long warmup_count;
    unsigned long long drain_count;
    unsigned long long active_count;
    perf::multi::bench_latency_stats_t latency;

    bench_result_t ()
        : warmup_count (0), drain_count (0), active_count (0), latency ()
    {
    }
};

class spot_client_bench_t
{
  public:
    spot_client_bench_t (const std::string &transport,
                         size_t msg_size,
                         const std::string &ready_payload,
                         const perf::multi::multi_bench_settings_t &settings)
        : _transport (transport),
          _msg_size (msg_size),
          _ready_payload (ready_payload),
          _settings (settings),
          _spot_client_count (settings.clients),
          _ctx (),
          _registry_pub_endpoint (),
          _registry_router_endpoint (),
          _server_endpoint (),
          _discovery (_ctx.ctx (), zlink::service_type::spot),
          _nodes (),
          _spots (),
          _poller (),
          _poll_events (),
          _ready_node (NULL),
          _payload_buffer (std::max<size_t> (msg_size, perf_metric::header_size ())),
          _phase_cfg (),
          _result ()
    {
        _nodes.reserve (_spot_client_count);
        _spots.reserve (_spot_client_count);
        _poll_events.reserve (_spot_client_count);

        _phase_cfg.warmup_seconds = std::max (0, _settings.warmup_seconds);
        _phase_cfg.settle_ms = std::max (0, _settings.settle_ms);
        _phase_cfg.active_seconds = std::max (1, _settings.duration_seconds);
    }

    bool run ()
    {
        if (!parse_ready_payload (_ready_payload,
                                  &_server_endpoint,
                                  &_registry_pub_endpoint,
                                  &_registry_router_endpoint)) {
            if (perf_debug_enabled ())
                std::cerr << "spot client: invalid ready payload" << std::endl;
            return false;
        }
        if (!setup_discovery ()) {
            if (perf_debug_enabled ())
                std::cerr << "spot client: setup discovery failed" << std::endl;
            return false;
        }
        if (!setup_nodes ()) {
            if (perf_debug_enabled ())
                std::cerr << "spot client: setup nodes failed" << std::endl;
            return false;
        }

        if (!wait_all_sub_peers (_nodes, _settings.connect_ready_timeout_ms)) {
            if (perf_debug_enabled ())
                std::cerr << "spot client: not all peers became ready" << std::endl;
            return false;
        }
        if (_ready_node && perf_debug_enabled ()) {
            size_t peer_count = 0;
            if (_ready_node->sub_peers (NULL, &peer_count) == 0)
                std::cerr << "spot client: sub peers=" << peer_count << std::endl;
        }

        perf::multi::settle ();

        if (!wait_for_msg_size_start ()) {
            if (perf_debug_enabled ())
                std::cerr << "spot client: msg size sync failed" << std::endl;
            return false;
        }
        if (!run_active ()) {
            if (perf_debug_enabled ())
                std::cerr << "spot client: active failed" << std::endl;
            return false;
        }

        if (_result.active_count == 0) {
            if (perf_debug_enabled ())
                std::cerr << "spot client: no active messages" << std::endl;
            return false;
        }

        print_result ();
        return true;
    }

  private:
    bool setup_discovery ()
    {
        if (!_discovery.valid ()
            || _discovery.connect_registry (_registry_router_endpoint) != 0
            || _discovery.subscribe (k_service_name) != 0) {
            return false;
        }

        const auto deadline = std::chrono::steady_clock::now ()
                              + std::chrono::milliseconds (
                                std::max (_settings.connect_ready_timeout_ms, 1000));
        while (std::chrono::steady_clock::now () < deadline) {
            if (_discovery.receiver_count (k_service_name) > 0)
                return true;
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
        return _discovery.receiver_count (k_service_name) > 0;
    }

    bool setup_nodes ()
    {
        for (size_t i = 0; i < _spot_client_count; ++i) {
            _nodes.emplace_back (new zlink::service::spot_node_t (_ctx.ctx ()));
            zlink::service::spot_node_t &node = *_nodes.back ();
            const int xpub_nodrop =
              perf::multi::parse_positive_env ("PERF_MULTI_SPOT_XPUB_NODROP", 1) > 0
                ? 1
                : 0;

            (void) node.set_sockopt (zlink::spot_node_socket_role::sub,
                                     zlink::socket_options::rcvhwm,
                                     _settings.rcvhwm);
            (void) node.set_sockopt (zlink::spot_node_socket_role::sub,
                                     zlink::socket_options::rcvtimeo,
                                     _settings.rcvtimeo_ms);
            (void) node.set_sockopt (zlink::spot_node_socket_role::pub,
                                     zlink::socket_options::xpub_nodrop,
                                     xpub_nodrop);

            if (!configure_spot_client_tls (node, _transport)) {
                if (perf_debug_enabled ()) {
                    std::cerr << "spot client: tls configure failed" << std::endl;
                }
                return false;
            }
            const std::string bind_endpoint =
              perf::multi::make_endpoint (
                _transport, std::string ("cpp_multi_spot_client_") + std::to_string (i), 0);
            if (bind_endpoint.empty () || node.bind (bind_endpoint) != 0
                || node.set_discovery (_discovery.handle (), k_service_name) != 0) {
                if (perf_debug_enabled ())
                    std::cerr << "spot client: bind/discovery setup failed" << std::endl;
                return false;
            }

            if (!_ready_node)
                _ready_node = &node;

            _spots.emplace_back (new zlink::service::spot_t (node));
            if (!_spots.back ()->valid ()) {
                if (perf_debug_enabled ())
                    std::cerr << "spot client: spot create failed" << std::endl;
                return false;
            }
            if (_spots.back ()->subscribe (k_topic) != 0) {
                if (perf_debug_enabled ())
                    std::cerr << "spot client: subscribe failed" << std::endl;
                return false;
            }
            if (_poller.add_spot_sub (
                  *_spots.back (), zlink::poll_event::pollin, _spots.back ().get ())
                != 0) {
                if (perf_debug_enabled ())
                    std::cerr << "spot client: add poller failed" << std::endl;
                return false;
            }
        }

        for (size_t i = 0; i < _nodes.size (); ++i) {
            if (_nodes[i]->set_discovery (_discovery.handle (), k_service_name) != 0)
                return false;
        }

        return !_spots.empty ();
    }

    int recv_payload_header (zlink::service::spot_t &subscriber,
                             zlink::recv_flag flags,
                             perf_metric::header_t *header_out,
                             bool *header_ok_out)
    {
        if (header_ok_out)
            *header_ok_out = false;

        std::string topic;
        size_t payload_rc = 0;
        if (subscriber.recv (
              topic,
              _payload_buffer.data (),
              _payload_buffer.size (),
              &payload_rc,
              flags)
            != 0) {
            const int err = errno;
            if (err == EAGAIN || err == EINTR)
                return 0;
            return -1;
        }

        if (topic != k_topic) {
            perf_debug_recv_once (
              std::string ("spot client: topic mismatch topic=") + topic);
            return 1;
        }

        bool header_ok = false;
        if (header_out) {
            header_ok = perf_metric::decode_payload_header (
              _payload_buffer.data (), payload_rc, header_out);
        }
        if (!header_ok) {
            perf_debug_recv_once (
              std::string ("spot client: header decode failed payload=")
              + std::to_string (payload_rc));
        }

        if (header_ok_out)
            *header_ok_out = header_ok;
        return 1;
    }

    bool run_recv_phase (perf_metric::phase_t phase,
                         std::chrono::milliseconds duration,
                         unsigned long long *count_out,
                         perf::multi::bench_latency_sampler_t *lat_out)
    {
        if (!count_out)
            return false;

        if (duration.count () <= 0) {
            *count_out = 0;
            return true;
        }

        if (_spots.empty ())
            return false;

        unsigned long long count = 0;

        const bool active_phase = phase == perf_metric::phase_active;
        const bool phase_wildcard = phase == perf_metric::phase_unknown;
        const auto deadline = std::chrono::steady_clock::now () + duration;

        while (std::chrono::steady_clock::now () < deadline) {
            const auto now = std::chrono::steady_clock::now ();
            const auto phase_deadline = deadline;
            long wait_ms = 100;
            const long remain_ms =
              std::chrono::duration_cast<std::chrono::milliseconds> (phase_deadline
                                                                      - now)
                .count ();
            if (remain_ms < wait_ms)
                wait_ms = remain_ms;
            if (wait_ms < 1)
                wait_ms = 1;

            const int poll_rc = _poller.wait (_poll_events, wait_ms);
            if (poll_rc < 0) {
                if (errno == EINTR)
                    continue;
                return false;
            }
            if (poll_rc == 0) {
                if (perf_debug_enabled () && active_phase)
                    perf_debug_recv_once ("spot client: poll timeout in active phase");
                continue;
            }

            for (size_t i = 0; i < _poll_events.size (); ++i) {
                zlink::service::spot_t *subscriber =
                  static_cast<zlink::service::spot_t *> (_poll_events[i].user);
                if (!subscriber)
                    continue;

                for (;;) {
                    perf_metric::header_t header = perf_metric::header_t ();
                    bool header_ok = false;
                    const int recv_rc = recv_payload_header (
                      *subscriber,
                      zlink::recv_flag::dontwait,
                      &header,
                      &header_ok);
                    if (recv_rc == 0)
                        break;
                    if (recv_rc < 0)
                        return false;
                    if (!header_ok)
                        continue;
                    if (header.magic != perf_metric::k_magic
                        || header.run_id != k_run_id
                        || header.msg_size != static_cast<uint32_t> (_msg_size)) {
                        continue;
                    }

                    if (active_phase) {
                        if (header.phase
                            != static_cast<uint32_t> (perf_metric::phase_active)) {
                            continue;
                        }
                    } else if (!phase_wildcard
                               && header.phase != static_cast<uint32_t> (phase))
                        continue;

                    ++count;
                    if (lat_out && phase == perf_metric::phase_active) {
                        const uint64_t now_us = perf_metric::now_us ();
                        const double latency_us = now_us >= header.sent_ts_us
                                                    ? static_cast<double> (
                                                        now_us - header.sent_ts_us)
                                                    : 0.0;
                        lat_out->add (latency_us);
                    }
                }
            }
        }

        *count_out = count;
        return true;
    }

    bool wait_for_msg_size_start ()
    {
        const int sync_timeout_ms =
          std::max (5000,
                    std::max (_settings.connect_ready_timeout_ms,
                              std::max (_settings.connect_ready_timeout_ms * 4,
                                        _settings.warmup_seconds * 1000
                                          + _settings.settle_ms + 5000)));
        unsigned long long sync_count = 0;
        return run_recv_phase (perf_metric::phase_unknown,
                               std::chrono::milliseconds (sync_timeout_ms),
                               &sync_count,
                               NULL)
               && sync_count > 0;
    }

    bool run_active ()
    {
        perf::multi::bench_latency_sampler_t latency;
        if (!run_recv_phase (perf_metric::phase_unknown,
                             std::chrono::seconds (_phase_cfg.active_seconds),
                             &_result.active_count,
                             &latency)) {
            return false;
        }

        _result.latency = latency.snapshot ();
        return true;
    }

    void print_result () const
    {
        const double throughput = static_cast<double> (_result.active_count)
                                  / static_cast<double> (_phase_cfg.active_seconds);
        const double bandwidth = throughput * static_cast<double> (_msg_size) / 1000000.0;

        perf::multi::print_result ("current",
                                   k_pattern_result,
                                   _transport,
                                   _msg_size,
                                   throughput,
                                   bandwidth,
                                   _result.latency.mean_us,
                                   _result.latency.p95_us,
                                   _result.latency.p99_us);
    }

  private:
    const std::string _transport;
    const size_t _msg_size;
    const std::string _ready_payload;
    const perf::multi::multi_bench_settings_t _settings;
    const size_t _spot_client_count;

    perf::multi::ctx_guard_t _ctx;
    std::string _registry_pub_endpoint;
    std::string _registry_router_endpoint;
    std::string _server_endpoint;
    zlink::service::discovery_t _discovery;
    std::vector<std::unique_ptr<zlink::service::spot_node_t> > _nodes;
    std::vector<std::unique_ptr<zlink::service::spot_t> > _spots;
    zlink::poller_t _poller;
    std::vector<zlink::poll_event_t> _poll_events;
    zlink::service::spot_node_t *_ready_node;
    std::vector<char> _payload_buffer;

    phase_config_t _phase_cfg;
    bench_result_t _result;
};

} // namespace

bool perf_spot_client (const std::string &transport,
                       size_t msg_size,
                       const std::string &endpoint)
{
    perf::multi::set_perf_pattern_env (k_pattern_env);

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << k_pattern_result << "," << transport << std::endl;
        return true;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();

    spot_client_bench_t bench (transport, msg_size, endpoint, settings);
    return bench.run ();
}

int main (int argc, char **argv)
{
    if (argc < 3) {
        std::cerr << "usage: <transport> <size> --endpoint <endpoint>" << std::endl;
        return 1;
    }

    const std::string transport = argv[1];
    const size_t size = static_cast<size_t> (std::strtoull (argv[2], NULL, 10));
    if (size == 0)
        return 1;

    const std::string endpoint = perf::multi::parse_endpoint_arg (argc, argv);
    if (endpoint.empty ()) {
        std::cerr << "missing --endpoint" << std::endl;
        return 1;
    }

    return perf_spot_client (transport, size, endpoint) ? 0 : 1;
}
