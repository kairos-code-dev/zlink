// GATEWAY multi client benchmark: gateway service echo workload.
// Topology: client gateway_t(connect to discovery, N) <-> server receiver_t(bind, 1)
// Measurement: active-phase echo throughput + RTT latency from payload header.

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
#include <string>
#include <thread>
#include <vector>

namespace {

static const char *k_pattern_env = "GATEWAY";
static const char *k_pattern_result = "MULTI_GATEWAY";
static const char *k_service_name = "svc";
static const char k_payload_fill = 'g';
static const int k_discovery_ready_timeout_ms = 3000;
static const int k_gateway_ready_min_timeout_ms = 1000;

bool configure_gateway_tls (zlink::service::gateway_t &gateway,
                            const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    std::string cert;
    std::string key;
    std::string ca;
    if (!perf::multi::try_resolve_perf_tls_paths (cert, key, ca))
        return false;

    return gateway.set_tls_client (ca, "localhost", 0) == 0;
}

bool wait_discovery_ready (zlink::service::discovery_t &discovery,
                           const std::string &service,
                           int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (timeout_ms);
    while (std::chrono::steady_clock::now () < deadline) {
        if (discovery.service_available (service) > 0)
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
    return false;
}

bool wait_gateway_ready (zlink::service::gateway_t &gateway,
                         const std::string &service,
                         int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (
                            std::max (timeout_ms, k_gateway_ready_min_timeout_ms));
    while (std::chrono::steady_clock::now () < deadline) {
        if (gateway.connection_count (service) > 0)
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }

    return gateway.connection_count (service) > 0;
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
    unsigned long long active_count;
    perf::multi::bench_latency_stats_t latency;

    bench_result_t () : warmup_count (0), active_count (0), latency () {}
};

class gateway_client_bench_t
{
  public:
    gateway_client_bench_t (const std::string &transport,
                            size_t msg_size,
                            const std::string &endpoint,
                            const perf::multi::multi_bench_settings_t &settings)
        : _transport (transport),
          _msg_size (msg_size),
          _endpoint (endpoint),
          _settings (settings),
          _ctx (),
          _discovery (_ctx.ctx (), zlink::service_type::gateway),
          _holders (),
          _gateways (),
          _gateway_router_wrappers (),
          _gateway_routers (),
          _poller (),
          _poll_events (),
          _payload (std::max<size_t> (msg_size, perf_metric::header_size ()),
                    k_payload_fill),
          _run_id (static_cast<uint32_t> (perf_metric::now_us ())),
          _seq (1),
          _phase_cfg (),
          _result ()
    {
        _holders.reserve (_settings.clients);
        _gateways.reserve (_settings.clients);
        _gateway_router_wrappers.reserve (_settings.clients);
        _gateway_routers.reserve (_settings.clients);
        _poll_events.reserve (_settings.clients);

        _phase_cfg.warmup_seconds = std::max (0, _settings.warmup_seconds);
        _phase_cfg.settle_ms = std::max (0, _settings.settle_ms);
        _phase_cfg.active_seconds = std::max (1, _settings.duration_seconds);
    }

    bool run ()
    {
        if (!setup_discovery ())
            return false;

        if (!setup_gateways ())
            return false;

        perf::multi::settle ();

        if (!run_warmup ())
            return false;
        if (!run_settle ())
            return false;
        if (!run_active ())
            return false;

        send_stop_token_once ();
        print_result ();
        return true;
    }

  private:
    bool setup_discovery ()
    {
        if (!_discovery.valid () || _discovery.connect_registry (_endpoint) != 0
            || _discovery.subscribe (k_service_name) != 0) {
            return false;
        }

        return wait_discovery_ready (_discovery,
                                     k_service_name,
                                     k_discovery_ready_timeout_ms);
    }

    bool setup_gateways ()
    {
        for (size_t i = 0; i < _settings.clients; ++i) {
            const std::string routing_id = std::string ("gw_") + std::to_string (i);
            _holders.emplace_back (
              new zlink::service::gateway_t (_ctx.ctx (), _discovery, routing_id));
            zlink::service::gateway_t &gateway = *_holders.back ();

            if (!gateway.valid ())
                return false;

            (void) gateway.set_sockopt (zlink::socket_options::sndhwm, _settings.sndhwm);
            (void) gateway.set_sockopt (zlink::socket_options::rcvhwm, _settings.rcvhwm);
            (void) gateway.set_sockopt (zlink::socket_options::sndtimeo,
                                        _settings.sndtimeo_ms);
            (void) gateway.set_sockopt (zlink::socket_options::rcvtimeo,
                                        _settings.rcvtimeo_ms);

            if (!configure_gateway_tls (gateway, _transport))
                return false;

            if (!wait_gateway_ready (
                  gateway, k_service_name, _settings.connect_ready_timeout_ms)) {
                return false;
            }

            _gateway_router_wrappers.emplace_back (
              zlink::socket_t::wrap (gateway.router_handle ()));
            _gateway_routers.push_back (&_gateway_router_wrappers.back ());
            _gateways.push_back (&gateway);
            (void) _poller.add (
              _gateway_router_wrappers.back (),
              zlink::poll_event::pollin,
              _gateway_routers.back ());
        }

        return !_gateways.empty () && _gateways.size () == _gateway_routers.size ();
    }

    long compute_wait_ms (const std::chrono::steady_clock::time_point &deadline) const
    {
        const auto now = std::chrono::steady_clock::now ();
        if (now >= deadline)
            return 1;

        long wait_ms =
          _settings.client_poll_timeout_ms > 0 ? _settings.client_poll_timeout_ms : 100;
        const long remain_ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                                 deadline - now)
                                 .count ();
        if (remain_ms < wait_ms)
            wait_ms = remain_ms;
        if (wait_ms < 1)
            wait_ms = 1;
        return wait_ms;
    }

    int recv_gateway_payload_header (zlink::socket_t &gateway_router,
                                     size_t payload_size,
                                     zlink::recv_flag flags,
                                     perf_metric::header_t *header_out,
                                     bool *header_ok_out)
    {
        if (header_ok_out)
            *header_ok_out = false;

        zlink::message_t first;
        const int first_rc = gateway_router.recv (first, flags);
        if (first_rc < 0) {
            const int err = errno;
            if (err == EAGAIN || err == EINTR)
                return 0;
            return -1;
        }

        zlink::message_t payload_part;
        if (first.more ()) {
            if (gateway_router.recv (payload_part, flags) < 0) {
                const int err = errno;
                if (err == EAGAIN || err == EINTR)
                    return 0;
                return -1;
            }
            if (payload_part.more () || payload_part.size () != payload_size)
                return -1;
        } else {
            if (first.size () != payload_size)
                return -1;
            payload_part = std::move (first);
        }

        bool header_ok = false;
        if (header_out) {
            header_ok = perf_metric::decode_payload_header (
              payload_part.data (), payload_part.size (), header_out);
        }

        if (header_ok_out)
            *header_ok_out = header_ok;
        return 1;
    }

    bool run_phase (perf_metric::phase_t phase,
                    int seconds,
                    unsigned long long *count_out,
                    perf::multi::bench_latency_sampler_t *lat_out)
    {
        if (!count_out)
            return false;

        if (seconds <= 0) {
            *count_out = 0;
            return true;
        }

        if (_gateways.empty () || _gateway_routers.size () != _gateways.size ())
            return false;

        unsigned long long count = 0;
        size_t send_index = 0;
        const auto deadline = std::chrono::steady_clock::now ()
                              + std::chrono::seconds (seconds);

        while (std::chrono::steady_clock::now () < deadline) {
            const size_t slot = send_index % _gateways.size ();
            zlink::service::gateway_t *gateway = _gateways[slot];
            ++send_index;
            if (!gateway)
                continue;

            const uint64_t sent_ts = perf_metric::now_us ();
            if (!perf_metric::stamp_payload (_payload.data (),
                                             _payload.size (),
                                             _run_id,
                                             phase,
                                             _msg_size,
                                             _seq++,
                                             sent_ts)) {
                continue;
            }

            if (gateway->send (k_service_name,
                               _payload.data (),
                               _payload.size (),
                               zlink::send_flag::none)
                != 0) {
                const int err = errno;
                if (err == EAGAIN || err == EINTR)
                    continue;
                return false;
            }

            bool got_reply = false;
            while (!got_reply && std::chrono::steady_clock::now () < deadline) {
                const int poll_rc = _poller.wait (_poll_events, compute_wait_ms (deadline));
                if (poll_rc < 0) {
                    if (errno == EINTR)
                        continue;
                    return false;
                }
                if (poll_rc == 0)
                    break;

                for (size_t i = 0; i < _poll_events.size (); ++i) {
                    zlink::socket_t *ready_router =
                      static_cast<zlink::socket_t *> (_poll_events[i].user);
                    if (!ready_router)
                        continue;

                    for (;;) {
                        perf_metric::header_t header;
                        bool header_ok = false;
                        const int recv_rc = recv_gateway_payload_header (
                          *ready_router,
                          _payload.size (),
                          zlink::recv_flag::dontwait,
                          &header,
                          &header_ok);
                        if (recv_rc == 0)
                            break;
                        if (recv_rc < 0)
                            return false;
                        if (!header_ok)
                            continue;
                        if (!perf_metric::is_expected (
                              header, _run_id, phase, _msg_size)) {
                            continue;
                        }

                        ++count;
                        got_reply = true;
                        if (lat_out && phase == perf_metric::phase_active) {
                            const uint64_t now_us = perf_metric::now_us ();
                            const double latency_us =
                              now_us >= header.sent_ts_us
                                ? static_cast<double> (now_us - header.sent_ts_us)
                                : 0.0;
                            lat_out->add (latency_us);
                        }
                    }
                }
            }
        }

        *count_out = count;
        return true;
    }

    bool run_settle ()
    {
        if (_phase_cfg.settle_ms <= 0 || _gateway_routers.empty ())
            return true;

        const size_t payload_size = std::max<size_t> (_msg_size, perf_metric::header_size ());
        const auto deadline = std::chrono::steady_clock::now ()
                              + std::chrono::milliseconds (_phase_cfg.settle_ms);
        while (std::chrono::steady_clock::now () < deadline) {
            const int poll_rc = _poller.wait (_poll_events, compute_wait_ms (deadline));
            if (poll_rc < 0) {
                if (errno == EINTR)
                    continue;
                return false;
            }
            if (poll_rc == 0)
                continue;

            for (size_t i = 0; i < _poll_events.size (); ++i) {
                zlink::socket_t *gateway_router =
                  static_cast<zlink::socket_t *> (_poll_events[i].user);
                if (!gateway_router)
                    continue;

                for (;;) {
                    perf_metric::header_t header;
                    bool header_ok = false;
                    const int recv_rc = recv_gateway_payload_header (
                      *gateway_router,
                      payload_size,
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
                        || header.phase
                             != static_cast<uint32_t> (perf_metric::phase_warmup)
                        || header.msg_size != static_cast<uint32_t> (_msg_size)
                        || header.run_id != _run_id) {
                        continue;
                    }
                }
            }
        }

        return true;
    }

    bool run_warmup ()
    {
        return run_phase (perf_metric::phase_warmup,
                          _phase_cfg.warmup_seconds,
                          &_result.warmup_count,
                          NULL);
    }

    bool run_active ()
    {
        perf::multi::bench_latency_sampler_t latency;
        if (!run_phase (perf_metric::phase_active,
                        _phase_cfg.active_seconds,
                        &_result.active_count,
                        &latency)) {
            return false;
        }

        _result.latency = latency.snapshot ();
        return true;
    }

    void send_stop_token_once ()
    {
        if (_gateways.empty () || !_gateways[0])
            return;

        const char *stop = perf::multi::k_stop_token;
        const size_t stop_len = std::strlen (stop);
        (void) _gateways[0]->send (k_service_name, stop, stop_len, zlink::send_flag::none);
    }

    void print_result () const
    {
        const double throughput = static_cast<double> (_result.active_count)
                                  / static_cast<double> (_phase_cfg.active_seconds);
        const double bandwidth =
          throughput * static_cast<double> (_msg_size) * 2.0 / 1000000.0;

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
    zlink::service::discovery_t _discovery;
    std::vector<std::unique_ptr<zlink::service::gateway_t> > _holders;
    std::vector<zlink::service::gateway_t *> _gateways;
    std::vector<zlink::socket_t> _gateway_router_wrappers;
    std::vector<zlink::socket_t *> _gateway_routers;
    zlink::poller_t _poller;
    std::vector<zlink::poll_event_t> _poll_events;

    std::vector<char> _payload;
    const uint32_t _run_id;
    uint64_t _seq;

    phase_config_t _phase_cfg;
    bench_result_t _result;
};

} // namespace

void perf_gateway_client (const std::string &transport,
                          size_t msg_size,
                          const std::string &endpoint)
{
    perf::multi::set_perf_pattern_env (k_pattern_env);

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << k_pattern_result << "," << transport << std::endl;
        return;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();

    gateway_client_bench_t bench (transport, msg_size, endpoint, settings);
    (void) bench.run ();
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

    perf_gateway_client (transport, size, endpoint);
    return 0;
}
