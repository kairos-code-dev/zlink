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
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

static const char *k_pattern_env = "SPOT";
static const char *k_pattern_result = "MULTI_SPOT";
static const uint32_t k_run_id = 1;
static const int k_active_search_extension_seconds = 2;
static const char *k_topic = "bench";

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
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }

    size_t peer_count = 0;
    return node.sub_peers (NULL, &peer_count) == 0 && peer_count > 0;
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
                         const std::string &endpoint,
                         const perf::multi::multi_bench_settings_t &settings)
        : _transport (transport),
          _msg_size (msg_size),
          _endpoint (endpoint),
          _settings (settings),
          _spot_client_count (settings.clients),
          _ctx (),
          _nodes (),
          _sub_wrappers (),
          _poller (),
          _poll_events (),
          _ready_node (NULL),
          _phase_cfg (),
          _result ()
    {
        _nodes.reserve (_spot_client_count);
        _sub_wrappers.reserve (_spot_client_count);
        _poll_events.reserve (_spot_client_count);

        _phase_cfg.warmup_seconds = std::max (0, _settings.warmup_seconds);
        _phase_cfg.settle_ms = std::max (0, _settings.settle_ms);
        _phase_cfg.active_seconds = std::max (1, _settings.duration_seconds);
    }

    bool run ()
    {
        if (!setup_nodes ())
            return false;

        if (_ready_node)
            (void) wait_sub_peer_ready (*_ready_node, _settings.connect_ready_timeout_ms);

        perf::multi::settle ();

        if (!run_warmup ())
            return false;
        if (!run_settle ())
            return false;
        if (!run_active ())
            return false;

        if (_result.active_count == 0)
            return false;

        print_result ();
        return true;
    }

  private:
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
                                     zlink::socket_options::sndhwm,
                                     _settings.sndhwm);
            (void) node.set_sockopt (zlink::spot_node_socket_role::sub,
                                     zlink::socket_options::rcvhwm,
                                     _settings.rcvhwm);
            (void) node.set_sockopt (zlink::spot_node_socket_role::sub,
                                     zlink::socket_options::sndtimeo,
                                     _settings.sndtimeo_ms);
            (void) node.set_sockopt (zlink::spot_node_socket_role::sub,
                                     zlink::socket_options::rcvtimeo,
                                     _settings.rcvtimeo_ms);
            (void) node.set_sockopt (zlink::spot_node_socket_role::pub,
                                     zlink::socket_options::xpub_nodrop,
                                     xpub_nodrop);
            (void) node.set_sockopt (zlink::spot_node_socket_role::sub,
                                     zlink::socket_options::xpub_nodrop,
                                     xpub_nodrop);

            if (!configure_spot_client_tls (node, _transport)
                || node.connect_peer_pub (_endpoint) != 0) {
                return false;
            }

            if (!_ready_node)
                _ready_node = &node;

            _sub_wrappers.emplace_back (zlink::socket_t::wrap (node.sub_socket_handle ()));
            if (!_sub_wrappers.back ().handle ())
                return false;
            if (_sub_wrappers.back ().set (zlink::socket_options::subscribe,
                                           std::string (k_topic))
                != 0) {
                return false;
            }
            (void) _poller.add (
              _sub_wrappers.back (), zlink::poll_event::pollin, &_sub_wrappers.back ());
        }

        return !_sub_wrappers.empty ();
    }

    int recv_payload_header (zlink::socket_t &subscriber,
                             zlink::recv_flag flags,
                             perf_metric::header_t *header_out,
                             bool *header_ok_out)
    {
        if (header_ok_out)
            *header_ok_out = false;

        zlink::message_t topic;
        const int topic_rc = subscriber.recv (topic, flags);
        if (topic_rc < 0) {
            const int err = errno;
            if (err == EAGAIN || err == EINTR)
                return 0;
            return -1;
        }
        if (!topic.more ())
            return -1;

        zlink::message_t payload;
        const int payload_rc = subscriber.recv (payload, zlink::recv_flag::none);
        if (payload_rc < 0) {
            const int err = errno;
            if (err == EAGAIN || err == EINTR)
                return 0;
            return -1;
        }
        if (payload.more ())
            return -1;

        if (topic.size () != std::strlen (k_topic)
            || std::memcmp (topic.data (), k_topic, topic.size ()) != 0) {
            return 1;
        }

        bool header_ok = false;
        if (header_out) {
            header_ok = perf_metric::decode_payload_header (
              payload.data (), payload.size (), header_out);
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

        if (_sub_wrappers.empty ())
            return false;

        unsigned long long count = 0;

        const bool active_phase = phase == perf_metric::phase_active;
        auto deadline = std::chrono::steady_clock::now () + duration;
        const auto active_search_deadline =
          deadline + std::chrono::seconds (k_active_search_extension_seconds);
        bool active_started = !active_phase;

        while (std::chrono::steady_clock::now ()
               < (active_started ? deadline : active_search_deadline)) {
            const auto now = std::chrono::steady_clock::now ();
            const auto phase_deadline = active_started ? deadline : active_search_deadline;
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
            if (poll_rc == 0)
                continue;

            for (size_t i = 0; i < _poll_events.size (); ++i) {
                zlink::socket_t *subscriber =
                  static_cast<zlink::socket_t *> (_poll_events[i].user);
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
                        if (!active_started) {
                            active_started = true;
                            deadline = std::chrono::steady_clock::now () + duration;
                        }
                    } else if (header.phase != static_cast<uint32_t> (phase))
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

    bool run_warmup ()
    {
        return run_recv_phase (perf_metric::phase_warmup,
                               std::chrono::seconds (_phase_cfg.warmup_seconds),
                               &_result.warmup_count,
                               NULL);
    }

    bool run_settle ()
    {
        return run_recv_phase (perf_metric::phase_drain,
                               std::chrono::milliseconds (_phase_cfg.settle_ms),
                               &_result.drain_count,
                               NULL);
    }

    bool run_active ()
    {
        perf::multi::bench_latency_sampler_t latency;
        if (!run_recv_phase (perf_metric::phase_active,
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
    const std::string _endpoint;
    const perf::multi::multi_bench_settings_t _settings;
    const size_t _spot_client_count;

    perf::multi::ctx_guard_t _ctx;
    std::vector<std::unique_ptr<zlink::service::spot_node_t> > _nodes;
    std::vector<zlink::socket_t> _sub_wrappers;
    zlink::poller_t _poller;
    std::vector<zlink::poll_event_t> _poll_events;
    zlink::service::spot_node_t *_ready_node;

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
