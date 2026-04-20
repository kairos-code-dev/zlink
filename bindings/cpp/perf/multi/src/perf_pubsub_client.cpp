// PUBSUB multi client benchmark: one-way subscriber receive workload.
// Topology: server PUB(bind, 1) -> client SUB(connect, N)
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
#include <memory>
#include <vector>

namespace {

static const char *k_pattern_env = "PUBSUB";
static const char *k_pattern_result = "MULTI_PUBSUB";
static const char *k_topic = "bench";
static const uint32_t k_run_id = 1;
struct phase_config_t
{
    int active_seconds;
};

struct bench_result_t
{
    unsigned long long warmup_count;
    unsigned long long active_count;
    perf::multi::bench_latency_stats_t latency;

    bench_result_t ()
        : warmup_count (0), active_count (0), latency ()
    {
    }
};

class pubsub_client_bench_t
{
  public:
    pubsub_client_bench_t (const std::string &transport,
                           size_t msg_size,
                           const std::string &endpoint,
                           const perf::multi::multi_bench_settings_t &settings)
        : _transport (transport),
          _msg_size (msg_size),
          _endpoint (endpoint),
          _settings (settings),
          _ctx (),
          _holders (),
          _monitors (),
          _sockets (),
          _poller (),
          _poll_events (),
          _recv_buffer (std::max<size_t> (msg_size, perf_metric::header_size ())),
          _phase_cfg (),
          _result (),
          _failure_stage ("init")
    {
        _holders.reserve (_settings.clients);
        _monitors.reserve (_settings.clients);
        _sockets.reserve (_settings.clients);
        _poll_events.reserve (_settings.clients);

        _phase_cfg.active_seconds = std::max (1, _settings.duration_seconds);
    }

    bool run ()
    {
        if (!setup_sockets ())
            return false;

        std::cout << "CLIENT_READY," << _msg_size << std::endl;

        _resource_probe_start = perf::multi::start_resource_probe ();
        if (!run_phase (perf_metric::phase_active,
                        std::chrono::seconds (_phase_cfg.active_seconds),
                        &_result.active_count,
                        &_result.latency))
            return false;

        _resource_metrics =
          perf::multi::finish_resource_probe (_resource_probe_start);
        if (_result.active_count == 0) {
            _failure_stage = "no_active_data";
            return false;
        }

        print_result ();
        std::cout << "CLIENT_DONE," << _msg_size << std::endl;
        return true;
    }

    const char *failure_stage () const
    {
        return _failure_stage;
    }

    unsigned long long warmup_count () const
    {
        return _result.warmup_count;
    }

    unsigned long long active_count () const
    {
        return _result.active_count;
    }

  private:
    void close_monitors ()
    {
        for (size_t i = 0; i < _monitors.size (); ++i)
            perf::multi::close_connect_monitor (_monitors[i]);
    }

    bool setup_sockets ()
    {
        for (size_t i = 0; i < _settings.clients; ++i) {
            _holders.emplace_back (
              new perf::multi::socket_guard_t (_ctx, zlink::socket_type::sub));
            zlink::socket_t &sock = _holders.back ()->sock ();
            _monitors.push_back (perf::multi::connect_monitor_t ());

            (void) sock.set_subscription (std::string (k_topic));
            perf::multi::apply_benchmark_socket_options (sock, _settings, _transport);
            if (!perf::multi::setup_tls_client (sock, _transport))
                return false;
            if (!perf::multi::open_socket_monitor (
                  sock,
                  zlink::monitor_event::connection_ready,
                  _monitors.back ())) {
                _failure_stage = "monitor_open";
                return false;
            }
            if (sock.connect (_endpoint) != 0)
                return false;

            _sockets.push_back (&sock);
            (void) _poller.add (sock, zlink::poll_event::pollin, &sock);
        }

        for (size_t i = 0; i < _monitors.size (); ++i) {
            if (!perf::multi::wait_socket_monitor_event (
                  *_monitors[i].monitor,
                  static_cast<uint64_t> (
                    zlink::monitor_event::connection_ready),
                  -1,
                  _settings.connect_ready_timeout_ms)) {
                _failure_stage = "connection_ready";
                close_monitors ();
                return false;
            }
        }

        close_monitors ();
        return !_sockets.empty ();
    }

    bool run_phase (perf_metric::phase_t phase,
                    std::chrono::milliseconds duration,
                    unsigned long long *count_out,
                    perf::multi::bench_latency_stats_t *lat_out)
    {
        if (duration.count () <= 0) {
            if (count_out)
                *count_out = 0;
            if (lat_out)
                *lat_out = perf::multi::bench_latency_stats_t ();
            return true;
        }

        if (_sockets.empty ())
            return false;

        perf::multi::bench_latency_sampler_t latency;
        unsigned long long count = 0;
        const bool active_phase = phase == perf_metric::phase_active;
        auto deadline = std::chrono::steady_clock::now () + duration;
        const int active_search_extension_seconds = 2;
        const auto active_search_deadline =
          deadline + std::chrono::seconds (active_search_extension_seconds);
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

            const int poll_rc = _poller.wait_all (_poll_events, wait_ms);
            if (poll_rc < 0) {
                if (errno == EINTR || errno == EAGAIN)
                    continue;
                return false;
            }
            if (poll_rc == 0)
                continue;

            for (size_t i = 0; i < _poll_events.size (); ++i) {
                zlink::socket_t *sock =
                  static_cast<zlink::socket_t *> (_poll_events[i].user);
                if (!sock)
                    continue;

                for (;;) {
                    zlink::topic_message_t subscribed;
                    const int recv_rc =
                      sock->subscribe (subscribed, zlink::recv_flags_t::dontwait);
                    if (recv_rc != 0) {
                        const int err = errno;
                        if (err == EAGAIN)
                            break;
                        if (err == EINTR)
                            continue;
                        return false;
                    }

                    if (subscribed.topic () != k_topic
                        || subscribed.parts ().empty ()) {
                        continue;
                    }

                    const size_t recv_size = subscribed.parts ()[0].size ();
                    if (recv_size > _recv_buffer.size ()) {
                        continue;
                    }
                    if (recv_size > 0) {
                        std::memcpy (_recv_buffer.data (),
                                     subscribed.parts ()[0].data (),
                                     recv_size);
                    }

                    if (!active_phase) {
                        ++count;
                        continue;
                    }

                    perf_metric::header_t header;
                    if (!perf_metric::decode_payload_header (
                          _recv_buffer.data (),
                          recv_size,
                          &header)) {
                        continue;
                    }
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
                        const uint64_t now_ns = perf_metric::now_ns ();
                        const double latency_ns = now_ns >= header.sent_ts_ns
                                                    ? static_cast<double> (
                                                        now_ns - header.sent_ts_ns)
                                                    : 0.0;
                        latency.add (latency_ns);
                    }
                }
            }
        }

        if (count_out)
            *count_out = count;
        if (lat_out)
            *lat_out = latency.snapshot ();
        return true;
    }

    void print_result () const
    {
        perf::multi::print_client_result_lines (
          k_pattern_result,
          _transport,
          _msg_size,
          _result.active_count,
          _phase_cfg.active_seconds,
          1.0,
          _result.latency,
          _resource_metrics);
    }

  private:
    const std::string _transport;
    const size_t _msg_size;
    const std::string _endpoint;
    const perf::multi::multi_bench_settings_t _settings;

    perf::multi::ctx_guard_t _ctx;
    std::vector<std::unique_ptr<perf::multi::socket_guard_t> > _holders;
    std::vector<perf::multi::connect_monitor_t> _monitors;
    std::vector<zlink::socket_t *> _sockets;
    zlink::poller_t _poller;
    std::vector<zlink::poll_event_t> _poll_events;
    std::vector<char> _recv_buffer;

    phase_config_t _phase_cfg;
    bench_result_t _result;
    const char *_failure_stage;
    bench_multi_cpu_sample_t _resource_probe_start;
    bench_multi_resource_metrics_t _resource_metrics;
};

} // namespace

bool perf_pubsub_client (const std::string &transport,
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

    pubsub_client_bench_t bench (transport, msg_size, endpoint, settings);
    if (!bench.run ()) {
        std::cerr << "PUBSUB_CLIENT_FAIL,stage=" << bench.failure_stage ()
                  << ",transport=" << transport << ",size=" << msg_size
                  << ",warmup=" << bench.warmup_count ()
                  << ",active=" << bench.active_count () << std::endl;
        return false;
    }

    return true;
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

    return perf_pubsub_client (transport, size, endpoint) ? 0 : 1;
}
