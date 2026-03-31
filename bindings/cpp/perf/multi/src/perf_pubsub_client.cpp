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
#include <memory>
#include <vector>

namespace {

static const char *k_pattern_env = "PUBSUB";
static const char *k_pattern_result = "MULTI_PUBSUB";
static const uint32_t k_run_id = 1;
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

        _phase_cfg.warmup_seconds = std::max (0, _settings.warmup_seconds);
        _phase_cfg.settle_ms = std::max (0, _settings.settle_ms);
        _phase_cfg.active_seconds = std::max (1, _settings.duration_seconds);
    }

    bool run ()
    {
        if (!setup_sockets ())
            return false;

        perf::multi::settle ();

        if (!run_warmup ())
            return false;
        if (!run_settle ())
            return false;
        if (!run_active ())
            return false;

        if (_result.active_count == 0) {
            _failure_stage = "no_active_data";
            return false;
        }

        print_result ();
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

    unsigned long long drain_count () const
    {
        return _result.drain_count;
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

            (void) sock.set_subscription (std::string ());
            perf::multi::apply_benchmark_socket_options (sock, _settings, _transport);
            if (!perf::multi::setup_tls_client (sock, _transport))
                return false;
            if (!perf::multi::open_connect_monitor (sock, _monitors.back ())) {
                _failure_stage = "monitor_open";
                return false;
            }
            if (sock.connect (_endpoint) != 0)
                return false;

            _sockets.push_back (&sock);
            (void) _poller.add (sock, zlink::poll_event::pollin, &sock);
        }

        for (size_t i = 0; i < _monitors.size (); ++i) {
            if (!perf::multi::wait_connect_ready (_monitors[i],
                                                  _settings.connect_ready_timeout_ms)) {
                _failure_stage = "connect_ready";
                close_monitors ();
                return false;
            }
        }

        close_monitors ();

        return !_sockets.empty ();
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

        if (_sockets.empty ())
            return false;

        unsigned long long count = 0;
        const bool active_phase = phase == perf_metric::phase_active;
        auto deadline = std::chrono::steady_clock::now () + duration;
        const int active_search_extension_seconds =
          std::max (2,
                    _phase_cfg.warmup_seconds
                      + std::max (1, (_phase_cfg.settle_ms + 999) / 1000));
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
                if (errno == EINTR)
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
                    const int recv_size =
                      sock->recv (_recv_buffer.data (),
                                  _recv_buffer.size (),
                                  zlink::recv_flag::dontwait);
                    if (recv_size < 0) {
                        const int err = errno;
                        if (err == EAGAIN)
                            break;
                        if (err == EINTR)
                            continue;
                        return false;
                    }

                    if (!active_phase) {
                        ++count;
                        continue;
                    }

                    perf_metric::header_t header;
                    if (!perf_metric::decode_payload_header (
                          _recv_buffer.data (),
                          static_cast<size_t> (recv_size),
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
                  << ",drain=" << bench.drain_count ()
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
