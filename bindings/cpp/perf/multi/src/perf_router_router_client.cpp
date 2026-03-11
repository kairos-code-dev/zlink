// ROUTER-ROUTER multi client benchmark: routed echo request/reply workload.
// Topology: client ROUTER(connect, N) <-> server ROUTER(bind, routing_id=SERVER)
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

static const char *k_pattern_env = "ROUTER_ROUTER";
static const char *k_pattern_result = "MULTI_ROUTER_ROUTER";
static const char k_payload_fill = 'r';

bool recv_router_payload_header (zlink::socket_t &sock,
                                 size_t payload_size,
                                 zlink::recv_flag flags,
                                 perf_metric::header_t *header_out,
                                 bool *header_ok_out)
{
    if (header_ok_out)
        *header_ok_out = false;

    zlink::message_t routing_id;
    if (sock.recv (routing_id, flags) < 0)
        return false;
    if (!routing_id.more ()) {
        errno = EPROTO;
        return false;
    }

    zlink::message_t payload_or_delim;
    if (sock.recv (payload_or_delim, zlink::recv_flag::none) < 0)
        return false;

    zlink::message_t payload;
    if (payload_or_delim.more ()) {
        if (payload_or_delim.size () != 0) {
            errno = EPROTO;
            return false;
        }
        if (sock.recv (payload, zlink::recv_flag::none) < 0)
            return false;
    } else {
        payload = std::move (payload_or_delim);
    }

    if (payload.more () || payload.size () != payload_size) {
        errno = EPROTO;
        return false;
    }

    bool header_ok = false;
    if (header_out) {
        header_ok = perf_metric::decode_payload_header (
          payload.data (), payload.size (), header_out);
    }
    if (header_ok_out)
        *header_ok_out = header_ok;
    return true;
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

struct socket_state_t
{
    zlink::socket_t *sock;
    std::vector<char> payload;
    bool awaiting_reply;
    bool send_pending;
    bool pollout_enabled;
    bool id_sent;

    socket_state_t ()
        : sock (NULL),
          payload (),
          awaiting_reply (false),
          send_pending (false),
          pollout_enabled (false),
          id_sent (false)
    {
    }
};

class router_router_client_bench_t
{
  public:
    router_router_client_bench_t (const std::string &transport,
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
          _socket_states (),
          _poller (),
          _poll_events (),
          _run_id (static_cast<uint32_t> (perf_metric::now_us ())),
          _seq (1),
          _server_id ("SERVER"),
          _phase_cfg (),
          _result ()
    {
        _holders.reserve (_settings.clients);
        _monitors.reserve (_settings.clients);
        _socket_states.reserve (_settings.clients);
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
        if (_result.active_count == 0)
            return false;

        send_stop_token_once ();
        print_result ();
        return true;
    }

  private:
    bool setup_sockets ()
    {
        for (size_t i = 0; i < _settings.clients; ++i) {
            _holders.emplace_back (
              new perf::multi::socket_guard_t (_ctx, zlink::socket_type::router));
            zlink::socket_t &sock = _holders.back ()->sock ();

            const std::string routing_id = std::string ("rr_") + std::to_string (i);
            (void) sock.set (zlink::socket_options::routing_id, routing_id);
            (void) sock.set (zlink::socket_options::probe_router, 1);

            perf::multi::apply_benchmark_socket_options (sock, _settings, _transport);
            if (!perf::multi::setup_tls_client (sock, _transport))
                return false;
            _monitors.push_back (perf::multi::connect_monitor_t ());
            if (!perf::multi::open_connect_monitor (sock, _monitors.back ()))
                return false;
            if (sock.connect (_endpoint) != 0)
                return false;

            socket_state_t state;
            state.sock = &sock;
            state.payload.resize (
              std::max<size_t> (_msg_size, perf_metric::header_size ()),
              k_payload_fill);
            _socket_states.push_back (state);
            (void) _poller.add (sock, zlink::poll_event::pollin, &_socket_states.back ());
        }

        const bool ready = perf::multi::wait_all_connect_ready (
          _monitors, _settings.connect_ready_timeout_ms);
        for (size_t i = 0; i < _monitors.size (); ++i)
            perf::multi::close_connect_monitor (_monitors[i]);
        if (!ready)
            return false;

        return !_socket_states.empty ();
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

    bool set_pollout (socket_state_t &state, bool enabled)
    {
        if (!state.sock)
            return false;
        if (state.pollout_enabled == enabled)
            return true;

        const zlink::poll_event events =
          enabled ? (zlink::poll_event::pollin | zlink::poll_event::pollout)
                  : zlink::poll_event::pollin;
        if (_poller.modify (*state.sock, events) != 0)
            return false;
        state.pollout_enabled = enabled;
        return true;
    }

    bool try_send_request (socket_state_t &state, perf_metric::phase_t phase)
    {
        if (!state.sock)
            return false;

        if (!state.send_pending) {
            const uint64_t sent_ts = perf_metric::now_us ();
            if (!perf_metric::stamp_payload (state.payload.data (),
                                             state.payload.size (),
                                             _run_id,
                                             phase,
                                             _msg_size,
                                             _seq++,
                                             sent_ts)) {
                return false;
            }
            state.id_sent = false;
        }

        if (!state.id_sent) {
            const int id_sent = state.sock->send (
              _server_id.data (),
              _server_id.size (),
              zlink::send_flag::sndmore | zlink::send_flag::dontwait);
            if (id_sent != static_cast<int> (_server_id.size ())) {
                if (id_sent < 0 && errno == EAGAIN) {
                    state.send_pending = true;
                    return set_pollout (state, true);
                }
                return false;
            }
            state.id_sent = true;
        }

        const int payload_sent = state.sock->send (
          state.payload.data (), state.payload.size (), zlink::send_flag::dontwait);
        if (payload_sent == static_cast<int> (state.payload.size ())) {
            state.awaiting_reply = true;
            state.send_pending = false;
            state.id_sent = false;
            return set_pollout (state, false);
        }
        if (payload_sent < 0 && errno == EAGAIN) {
            state.send_pending = true;
            return set_pollout (state, true);
        }
        return false;
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

        if (_socket_states.empty ())
            return false;

        unsigned long long count = 0;
        size_t send_index = 0;
        const auto deadline = std::chrono::steady_clock::now ()
                              + std::chrono::seconds (seconds);
        while (std::chrono::steady_clock::now () < deadline) {
            for (size_t i = 0; i < _socket_states.size (); ++i) {
                socket_state_t &state =
                  _socket_states[(send_index + i) % _socket_states.size ()];
                if (!state.sock || state.awaiting_reply || state.send_pending)
                    continue;
                if (!try_send_request (state, phase))
                    return false;
            }
            ++send_index;

            const int poll_rc = _poller.wait (_poll_events, compute_wait_ms (deadline));
            if (poll_rc < 0) {
                if (errno == EINTR)
                    continue;
                return false;
            }
            if (poll_rc == 0)
                continue;

            for (size_t i = 0; i < _poll_events.size (); ++i) {
                socket_state_t *state =
                  static_cast<socket_state_t *> (_poll_events[i].user);
                zlink::socket_t *ready_sock = _poll_events[i].socket;
                if (!state || !ready_sock)
                    continue;

                if ((_poll_events[i].revents
                     & static_cast<short> (zlink::poll_event::pollout))
                    && state->send_pending) {
                    if (!try_send_request (*state, phase))
                        return false;
                }

                if (!(_poll_events[i].revents
                      & static_cast<short> (zlink::poll_event::pollin))) {
                    continue;
                }

                for (;;) {
                    perf_metric::header_t header;
                    bool header_ok = false;
                    if (!recv_router_payload_header (*ready_sock,
                                                     _socket_states[0].payload.size (),
                                                     zlink::recv_flag::dontwait,
                                                     &header,
                                                     &header_ok)) {
                        const int err = errno;
                        if (err == EAGAIN)
                            break;
                        if (err == EINTR)
                            continue;
                        return false;
                    }
                    state->awaiting_reply = false;
                    if (!header_ok)
                        continue;
                    if (!perf_metric::is_expected (header, _run_id, phase, _msg_size))
                        continue;

                    ++count;
                    state->awaiting_reply = false;
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

        *count_out = count;
        return true;
    }

    bool run_settle ()
    {
        if (_phase_cfg.settle_ms <= 0 || _socket_states.empty ())
            return true;

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
                socket_state_t *state =
                  static_cast<socket_state_t *> (_poll_events[i].user);
                zlink::socket_t *sock = _poll_events[i].socket;
                if (!state || !sock)
                    continue;

                if ((_poll_events[i].revents
                     & static_cast<short> (zlink::poll_event::pollout))
                    && state->send_pending) {
                    if (!try_send_request (*state, perf_metric::phase_drain))
                        return false;
                }

                if (!(_poll_events[i].revents
                      & static_cast<short> (zlink::poll_event::pollin))) {
                    continue;
                }

                for (;;) {
                    perf_metric::header_t header;
                    bool header_ok = false;
                    if (!recv_router_payload_header (*sock,
                                                     _socket_states[0].payload.size (),
                                                     zlink::recv_flag::dontwait,
                                                     &header,
                                                     &header_ok)) {
                        const int err = errno;
                        if (err == EAGAIN)
                            break;
                        if (err == EINTR)
                            continue;
                        return false;
                    }
                    state->awaiting_reply = false;
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
        if (_socket_states.empty () || !_socket_states[0].sock)
            return;

        zlink::socket_t *sock = _socket_states[0].sock;
        const int stop_id_sent = sock->send (_server_id.data (),
                                             _server_id.size (),
                                             zlink::send_flag::sndmore
                                               | zlink::send_flag::dontwait);
        if (stop_id_sent != static_cast<int> (_server_id.size ()))
            return;

        const char *stop = perf::multi::k_stop_token;
        const size_t stop_len = std::strlen (stop);
        (void) sock->send (stop, stop_len, zlink::send_flag::dontwait);
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
    std::vector<std::unique_ptr<perf::multi::socket_guard_t> > _holders;
    std::vector<perf::multi::connect_monitor_t> _monitors;
    std::vector<socket_state_t> _socket_states;
    zlink::poller_t _poller;
    std::vector<zlink::poll_event_t> _poll_events;

    const uint32_t _run_id;
    uint64_t _seq;
    const std::string _server_id;

    phase_config_t _phase_cfg;
    bench_result_t _result;
};

} // namespace

bool perf_router_router_client (const std::string &transport,
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

    router_router_client_bench_t bench (transport, msg_size, endpoint, settings);
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

    return perf_router_router_client (transport, size, endpoint) ? 0 : 1;
}
