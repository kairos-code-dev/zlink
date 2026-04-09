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

bool same_routing_id (const zlink_routing_id_t &lhs, const zlink_routing_id_t &rhs)
{
    return lhs.size == rhs.size
           && std::memcmp (lhs.data, rhs.data, lhs.size) == 0;
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
    zlink::socket_t *sock;
    std::vector<char> request_buffer;
    size_t payload_size;
    zlink::message_t reply;
    bool awaiting_reply;
    bool send_pending;
    bool pollout_enabled;

    socket_state_t ()
        : sock (NULL),
          request_buffer (),
          payload_size (0),
          reply (),
          awaiting_reply (false),
          send_pending (false),
          pollout_enabled (false)
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
          _server_rid (zlink::empty_routing_id ()),
          _phase_cfg (),
          _result ()
    {
        _holders.reserve (_settings.clients);
        _monitors.reserve (_settings.clients);
        _socket_states.reserve (_settings.clients);
        _poll_events.reserve (_settings.clients);

        _phase_cfg.active_seconds = std::max (1, _settings.duration_seconds);
        (void) zlink::routing_id_from (_server_id, &_server_rid);
    }

    bool run ()
    {
        if (!setup_sockets ())
            return false;
        if (!validate_routes_once ())
            return false;

        _resource_probe_start = perf::multi::start_resource_probe ();
        if (!run_phase (perf_metric::phase_active,
                        _phase_cfg.active_seconds,
                        &_result.active_count,
                        &_result.latency))
            return false;
        if (_result.active_count == 0)
            return false;

        send_stop_token_once ();
        _resource_metrics =
          perf::multi::finish_resource_probe (_resource_probe_start);
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
            (void) sock.set_routing_id (routing_id);
            (void) sock.set (zlink::router_options::probe, 1);

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
            _socket_states.push_back (state);
            socket_state_t &slot = _socket_states.back ();
            slot.payload_size =
              std::max<size_t> (_msg_size, perf_metric::header_size ());
            slot.request_buffer.assign (slot.payload_size, k_payload_fill);
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

    bool validate_routes_once ()
    {
        if (_socket_states.empty ())
            return false;

        std::vector<bool> validated (_socket_states.size (), false);
        size_t remaining = validated.size ();
        const auto deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::milliseconds (
            std::max (1, _settings.connect_ready_timeout_ms));

        for (size_t i = 0; i < _socket_states.size (); ++i) {
            socket_state_t &state = _socket_states[i];
            state.awaiting_reply = false;
            state.send_pending = false;
            if (!set_pollout (state, false)
                || !try_send_request (state, perf_metric::phase_warmup)) {
                return false;
            }
        }

        while (remaining > 0 && std::chrono::steady_clock::now () < deadline) {
            const int poll_rc =
              _poller.wait_all (_poll_events, compute_wait_ms (deadline));
            if (poll_rc < 0) {
                if (errno == EINTR || errno == EAGAIN)
                    continue;
                return false;
            }
            if (poll_rc == 0)
                continue;

            for (size_t i = 0; i < _poll_events.size (); ++i) {
                socket_state_t *state =
                  static_cast<socket_state_t *> (_poll_events[i].user);
                if (!state || !state->sock)
                    continue;

                const size_t slot_index =
                  static_cast<size_t> (state - &_socket_states[0]);
                if (slot_index >= validated.size ())
                    return false;

                if ((_poll_events[i].revents
                     & static_cast<short> (zlink::poll_event::pollout))
                    && state->send_pending) {
                    if (!try_send_request (*state, perf_metric::phase_warmup))
                        return false;
                }

                if (!(_poll_events[i].revents
                      & static_cast<short> (zlink::poll_event::pollin))) {
                    continue;
                }

                for (;;) {
                    zlink_routing_id_t source_rid = zlink::empty_routing_id ();
                    perf_metric::header_t header;
                    const int recv_rc =
                      recv_reply (*state, &source_rid, &header);
                    if (recv_rc < 0) {
                        const int err = errno;
                        if (err == EAGAIN)
                            break;
                        if (err == EINTR)
                            continue;
                        return false;
                    }

                    state->awaiting_reply = false;
                    if (recv_rc != 0 || !same_routing_id (source_rid, _server_rid))
                        continue;
                    if (!perf_metric::is_expected (
                          header, _run_id, perf_metric::phase_warmup, _msg_size)) {
                        continue;
                    }
                    if (validated[slot_index])
                        break;

                    validated[slot_index] = true;
                    --remaining;
                    if (!set_pollout (*state, false))
                        return false;
                    break;
                }
            }
        }

        return remaining == 0;
    }

    long compute_wait_ms (const std::chrono::steady_clock::time_point &deadline) const
    {
        const auto now = std::chrono::steady_clock::now ();
        if (now >= deadline)
            return 1;

        long wait_ms =
          _settings.client_poll_timeout_ms > 0 ? _settings.client_poll_timeout_ms : 10;
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
        if (!state.sock || state.request_buffer.empty ())
            return false;

        const uint64_t sent_ts = perf_metric::now_us ();
        if (!perf_metric::stamp_payload (&state.request_buffer[0],
                                         state.payload_size,
                                         _run_id,
                                         phase,
                                         _msg_size,
                                         _seq++,
                                         sent_ts)) {
            return false;
        }

        zlink::message_t request =
          perf::multi::message_from_external_buffer (
            state.request_buffer, state.request_buffer.size ());
        if (!request.valid ()) {
            return false;
        }

        const int payload_sent =
          state.sock->send (_server_rid, request, zlink::send_flag::dontwait);
        if (payload_sent == 0) {
            state.awaiting_reply = true;
            state.send_pending = false;
            return set_pollout (state, false);
        }

        const int err = errno;
        if (payload_sent < 0 && err == EAGAIN) {
            state.send_pending = true;
            errno = err;
            return set_pollout (state, true);
        }
        errno = err;
        return false;
    }

    int recv_reply (socket_state_t &state,
                    zlink_routing_id_t *source_rid_out,
                    perf_metric::header_t *header_out)
    {
        if (!state.sock || !source_rid_out || !header_out) {
            errno = EINVAL;
            return -1;
        }

        zlink::message_t reply;
        zlink_routing_id_t source_rid = zlink::empty_routing_id ();
        const int rc =
          state.sock->recv (source_rid, reply, zlink::recv_flag::dontwait);
        if (rc != 0)
            return -1;

        if (!reply.valid ()) {
            errno = EPROTO;
            return -1;
        }
        if (reply.size () != state.payload_size) {
            errno = EPROTO;
            return -1;
        }

        *source_rid_out = source_rid;
        if (!perf_metric::decode_payload_header (
              reply.data (), reply.size (), header_out)) {
            return 1;
        }

        return 0;
    }

    bool run_phase (perf_metric::phase_t phase,
                    int seconds,
                    unsigned long long *count_out,
                    perf::multi::bench_latency_stats_t *lat_out)
    {
        if (seconds <= 0) {
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
        const auto deadline = std::chrono::steady_clock::now ()
                              + std::chrono::seconds (seconds);

        for (size_t i = 0; i < _socket_states.size (); ++i) {
            socket_state_t &state = _socket_states[i];
            if (!state.sock || state.awaiting_reply || state.send_pending)
                continue;
            if (!try_send_request (state, phase))
                return false;
        }

        while (std::chrono::steady_clock::now () < deadline) {
            const int poll_rc =
              _poller.wait_all (_poll_events, compute_wait_ms (deadline));
            if (poll_rc < 0) {
                if (errno == EINTR || errno == EAGAIN)
                    continue;
                return false;
            }
            if (poll_rc == 0)
                continue;

            for (size_t i = 0; i < _poll_events.size (); ++i) {
                socket_state_t *state =
                  static_cast<socket_state_t *> (_poll_events[i].user);
                if (!state || !state->sock)
                    continue;

                if (!(_poll_events[i].revents
                      & static_cast<short> (zlink::poll_event::pollin))) {
                    if ((_poll_events[i].revents
                         & static_cast<short> (zlink::poll_event::pollout))
                        && state->send_pending) {
                        if (!try_send_request (*state, phase))
                            return false;
                    }
                    continue;
                }

                for (;;) {
                    zlink_routing_id_t source_rid = zlink::empty_routing_id ();
                    perf_metric::header_t header;
                    const int recv_rc =
                      recv_reply (*state, &source_rid, &header);
                    if (recv_rc < 0) {
                        const int err = errno;
                        if (err == EAGAIN)
                            break;
                        if (err == EINTR)
                            continue;
                        return false;
                    }
                    state->awaiting_reply = false;

                    if (recv_rc == 0
                        && same_routing_id (source_rid, _server_rid)
                        && perf_metric::is_expected (
                          header, _run_id, phase, _msg_size)) {
                        ++count;
                        if (lat_out && phase == perf_metric::phase_active) {
                            const uint64_t now_us = perf_metric::now_us ();
                            const double latency_us =
                              now_us >= header.sent_ts_us
                                ? static_cast<double> (now_us - header.sent_ts_us)
                                    * 0.5
                                : 0.0;
                            latency.add (latency_us);
                        }
                    }

                    if (std::chrono::steady_clock::now () >= deadline)
                        continue;

                    if (!state->send_pending) {
                        if (!try_send_request (*state, phase))
                            return false;
                    }
                }

                if ((_poll_events[i].revents
                     & static_cast<short> (zlink::poll_event::pollout))
                    && state->send_pending) {
                    if (!try_send_request (*state, phase))
                        return false;
                }
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

        zlink::socket_t *sock = _socket_states[0].sock;
        const char *stop = perf::multi::k_stop_token;
        const size_t stop_len = std::strlen (stop);
        zlink::message_t stop_msg (stop_len);
        if (!stop_msg.valid ())
            return;
        std::memcpy (stop_msg.data (), stop, stop_len);
        (void) sock->send (_server_rid, stop_msg, zlink::send_flag::dontwait);
    }

    void print_result () const
    {
        perf::multi::print_client_result_lines (
          k_pattern_result,
          _transport,
          _msg_size,
          _result.active_count,
          _phase_cfg.active_seconds,
          2.0,
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
    std::vector<socket_state_t> _socket_states;
    zlink::poller_t _poller;
    std::vector<zlink::poll_event_t> _poll_events;

    const uint32_t _run_id;
    uint64_t _seq;
    const std::string _server_id;
    zlink_routing_id_t _server_rid;

    phase_config_t _phase_cfg;
    bench_result_t _result;
    bench_multi_cpu_sample_t _resource_probe_start;
    bench_multi_resource_metrics_t _resource_metrics;
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
