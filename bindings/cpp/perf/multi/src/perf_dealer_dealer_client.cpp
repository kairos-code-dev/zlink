// DEALER-DEALER multi client benchmark: one-way DEALER send workload.
// Topology: client DEALER(connect, N) -> server DEALER(bind, 1)
// Measurement: active-phase send throughput + sender-side send latency sample.

#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_entry.hpp"
#include "../common/perf_client_helpers.hpp"
#include "../common/perf_metric_header.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {

static const char *k_pattern_env = "DEALER_DEALER";
static const char *k_pattern_result = "MULTI_DEALER_DEALER";
static const char k_payload_fill = 'd';

bool perf_debug_enabled ()
{
    return std::getenv ("PERF_DEBUG") != NULL;
}

void debug_log (const std::string &message_)
{
    if (!perf_debug_enabled ())
        return;
    std::cerr << "dealer_dealer client: " << message_ << std::endl;
}

struct phase_config_t
{
    int active_seconds;
};

struct bench_result_t
{
    unsigned long long warmup_count;
    unsigned long long active_count;
    perf::multi::bench_latency_stats_t latency;

    bench_result_t () : warmup_count (0), active_count (0), latency () {}
};

struct socket_state_t
{
    ::perf::socket_t *sock;
    bool pollout_enabled;
    bool pending;

    socket_state_t () : sock (NULL), pollout_enabled (false), pending (false) {}
};

class dealer_dealer_client_bench_t
{
  public:
    dealer_dealer_client_bench_t (const std::string &transport,
                                  const std::string &lib_name,
                                  size_t msg_size,
                                  const std::string &endpoint,
                                  const perf::multi::multi_bench_settings_t &settings)
        : _transport (transport),
          _lib_name (lib_name),
          _msg_size (msg_size),
          _endpoint (endpoint),
          _settings (settings),
          _ctx (),
          _holders (),
          _monitors (),
          _socket_states (),
          _poller (),
          _poll_events (),
          _payload (std::max<size_t> (msg_size, perf_metric::header_size ()),
                    k_payload_fill),
          _run_id (1U),
          _seq (1),
          _phase_cfg (),
          _result ()
    {
        _holders.reserve (_settings.clients);
        _monitors.reserve (_settings.clients);
        _socket_states.reserve (_settings.clients);
        _poll_events.reserve (_settings.clients);

        _phase_cfg.active_seconds = std::max (1, _settings.duration_seconds);
    }

    bool run ()
    {
        if (!setup_sockets ())
            return false;

        std::cout << "CLIENT_READY," << _msg_size << std::endl;
        if (!perf::multi::wait_for_start_from_stdin (_msg_size))
            return false;

        if (!run_phase (perf_metric::phase_active,
                        std::chrono::seconds (_phase_cfg.active_seconds),
                        &_result.active_count,
                        &_result.latency))
            return false;

        send_stop_token_once ();
        return _result.active_count > 0;
    }

  private:
    bool setup_sockets ()
    {
        for (size_t i = 0; i < _settings.clients; ++i) {
            _holders.emplace_back (
              new perf::multi::socket_guard_t (_ctx, zlink::socket_type::dealer));
            ::perf::socket_t &sock = _holders.back ()->sock ();
            if (!sock.handle ()) {
                debug_log ("socket create failed");
                return false;
            }

            perf::multi::apply_benchmark_socket_options (sock, _settings, _transport);
            if (!perf::multi::setup_tls_client (sock, _transport)) {
                debug_log ("setup tls failed");
                return false;
            }
            _monitors.push_back (perf::multi::connect_monitor_t ());
            if (!perf::multi::open_connect_monitor (sock, _monitors.back ())) {
                debug_log ("open connect monitor failed");
                return false;
            }
            try {
                sock.connect (_endpoint);
            } catch (const zlink::zlink_error_t &) {
                debug_log ("connect failed errno=" + std::to_string (errno));
                return false;
            }

            socket_state_t state;
            state.sock = &sock;
            _socket_states.push_back (state);
            (void) _poller.add (
              sock, zlink::poll_event::pollout,
              reinterpret_cast<std::uintptr_t> (&_socket_states.back ()));
            (void) _poller.modify (sock, static_cast<zlink::poll_event> (0));
        }

        const bool ready = perf::multi::wait_all_connect_ready (
          _monitors, _settings.connect_ready_timeout_ms);
        for (size_t i = 0; i < _monitors.size (); ++i)
            perf::multi::close_connect_monitor (_monitors[i]);
        if (!ready) {
            debug_log ("wait_all_connect_ready failed");
            return false;
        }

        return !_socket_states.empty ();
    }

    bool set_pollout (socket_state_t &state, bool enabled)
    {
        if (!state.sock)
            return false;
        if (state.pollout_enabled == enabled)
            return true;
        try {
            _poller.modify (*state.sock,
                            enabled ? zlink::poll_event::pollout
                                    : static_cast<zlink::poll_event> (0));
        }
        catch (const zlink::zlink_error_t &) {
            debug_log ("poller modify failed errno=" + std::to_string (errno));
            return false;
        }
        state.pollout_enabled = enabled;
        return true;
    }

    bool try_send_once (socket_state_t &state,
                        perf_metric::phase_t phase,
                        unsigned long long *count,
                        perf::multi::bench_latency_sampler_t *lat_out)
    {
        if (!state.sock)
            return false;

        const uint64_t sent_ts_ns = perf_metric::now_ns ();
        if (!perf_metric::stamp_payload (_payload.data (),
                                         _payload.size (),
                                         _run_id,
                                         phase,
                                         _msg_size,
                                         _seq++,
                                         sent_ts_ns)) {
            debug_log ("stamp payload failed");
            return false;
        }

        const auto t0 = std::chrono::steady_clock::now ();
        zlink::message_t request (_payload.size ());
        if (!request.valid ()) {
            debug_log ("request wrap failed");
            return false;
        }
        if (!_payload.empty ())
            std::memcpy (request.data (), &_payload[0], _payload.size ());

        const int sent =
          state.sock->send (request, zlink::send_flags_t::dontwait);
        if (sent == 0) {
            state.pending = false;
            if (!set_pollout (state, false))
                return false;
            if (count)
                ++(*count);
            if (lat_out && phase == perf_metric::phase_active) {
                const auto t1 = std::chrono::steady_clock::now ();
                const double ns = static_cast<double> (
                  std::chrono::duration_cast<std::chrono::nanoseconds> (t1 - t0)
                    .count ());
                lat_out->add (ns);
            }
            return true;
        }

        if (sent != 0 && errno == EAGAIN) {
            state.pending = true;
            return set_pollout (state, true);
        }

        debug_log ("send failed errno=" + std::to_string (errno));
        return false;
    }

    bool run_phase (perf_metric::phase_t phase,
                    std::chrono::steady_clock::duration duration,
                    unsigned long long *count_out,
                    perf::multi::bench_latency_stats_t *lat_out)
    {
        if (duration <= std::chrono::steady_clock::duration::zero ()) {
            if (count_out)
                *count_out = 0;
            if (lat_out)
                *lat_out = perf::multi::bench_latency_stats_t ();
            return true;
        }

        if (_socket_states.empty ())
            return false;

        perf::multi::bench_latency_sampler_t latency;
        unsigned long long count = 0;
        size_t index = 0;
        const auto deadline = std::chrono::steady_clock::now () + duration;
        while (std::chrono::steady_clock::now () < deadline) {
            bool progress = false;

            for (size_t i = 0; i < _socket_states.size (); ++i) {
                socket_state_t &state =
                  _socket_states[(index + i) % _socket_states.size ()];
                if (state.pending || !state.sock)
                    continue;
                if (!try_send_once (state, phase, &count, &latency))
                    return false;
                progress = true;
            }
            ++index;

            bool has_pending = false;
            for (size_t i = 0; i < _socket_states.size (); ++i) {
                if (_socket_states[i].pending) {
                    has_pending = true;
                    break;
                }
            }

            if (progress || !has_pending)
                continue;

            long wait_ms = _settings.client_poll_timeout_ms > 0
                             ? _settings.client_poll_timeout_ms
                             : 100;
            const long remain_ms =
              std::chrono::duration_cast<std::chrono::milliseconds> (
                deadline - std::chrono::steady_clock::now ())
                .count ();
            if (remain_ms < wait_ms)
                wait_ms = remain_ms;
            if (wait_ms < 1)
                wait_ms = 1;

            _poll_events = _poller.wait_all (wait_ms);
        const int poll_rc = static_cast<int> (_poll_events.size ());
            if (poll_rc < 0) {
                if (errno == EINTR || errno == EAGAIN)
                    continue;
                debug_log ("poller wait failed errno=" + std::to_string (errno));
                return false;
            }
            if (poll_rc == 0)
                continue;

            for (size_t i = 0; i < _poll_events.size (); ++i) {
                socket_state_t *state =
                  static_cast<socket_state_t *> (reinterpret_cast<void *> (_poll_events[i].user_token));
                if (!state || !state->pending
                    || !(static_cast<short> (_poll_events[i].revents) & static_cast<short> (zlink::poll_event::pollout))) {
                    continue;
                }
                if (!try_send_once (*state, phase, &count, &latency))
                    return false;
            }
        }

        if (count_out)
            *count_out = count;
        if (lat_out)
            *lat_out = latency.snapshot ();
        return true;
    }

    void send_stop_token_once ()
    {
        if (_socket_states.empty () || !_socket_states[0].sock)
            return;

        const char *stop = perf::multi::k_stop_token;
        const size_t stop_len = std::strlen (stop);
        zlink::message_t stop_part = zlink::message_t::from_bytes (stop, stop_len);
        if (!stop_part.valid ())
            return;
        (void) _socket_states[0].sock->send (stop_part,
                                             zlink::send_flags_t::dontwait);
    }

  private:
    const std::string _transport;
    const std::string _lib_name;
    const size_t _msg_size;
    const std::string _endpoint;
    const perf::multi::multi_bench_settings_t _settings;

    perf::multi::ctx_guard_t _ctx;
    std::vector<std::unique_ptr<perf::multi::socket_guard_t> > _holders;
    std::vector<perf::multi::connect_monitor_t> _monitors;
    std::vector<socket_state_t> _socket_states;
    zlink::poller_t _poller;
    std::vector<zlink::poll_event_t> _poll_events;

    std::vector<char> _payload;
    const uint32_t _run_id;
    uint64_t _seq;

    phase_config_t _phase_cfg;
    bench_result_t _result;
};

} // namespace

bool perf_dealer_dealer_client (const std::string &lib_name,
                                const std::string &transport,
                                size_t msg_size,
                                const std::string &endpoint)
{
    perf::multi::set_perf_pattern_env (k_pattern_env);

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern_result
                  << ","
                  << transport << std::endl;
        return true;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();

    dealer_dealer_client_bench_t bench (
      transport, lib_name, msg_size, endpoint, settings);
    if (!bench.run ()) {
        std::cerr << "DEALER_DEALER_CLIENT_FAIL,transport=" << transport
                  << ",size=" << msg_size << ",errno=" << errno << std::endl;
        return false;
    }

    return true;
}

int main (int argc, char **argv)
{
    if (argc < 4) {
        std::cerr << "usage: <lib_name> <transport> <size> --endpoint <endpoint>"
                  << std::endl;
        return 1;
    }

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t size = static_cast<size_t> (std::strtoull (argv[3], NULL, 10));
    if (size == 0)
        return 1;

    const std::string endpoint = perf::multi::parse_endpoint_arg (argc, argv);
    if (endpoint.empty ()) {
        std::cerr << "missing --endpoint" << std::endl;
        return 1;
    }

    return perf_dealer_dealer_client (lib_name, transport, size, endpoint) ? 0
                                                                            : 1;
}
