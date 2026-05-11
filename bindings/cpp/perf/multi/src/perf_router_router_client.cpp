// ROUTER-ROUTER multi client benchmark: routed echo request/reply workload.
// Topology: client ROUTER(connect, N) <-> server ROUTER(bind, routing_id=SERVER)
// Measurement: active-phase echo throughput + RTT latency from payload header.

#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_entry.hpp"
#include "../common/perf_client_helpers.hpp"
#include "../common/perf_metric_header.hpp"

#include <algorithm>
#include <any>
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

bool perf_debug_enabled ()
{
    return std::getenv ("PERF_DEBUG") != NULL;
}

void debug_log (const std::string &message_)
{
    if (!perf_debug_enabled ())
        return;
    std::cerr << "router_router client: " << message_ << std::endl;
}

zlink::routing_id_t routing_id_from_ascii (const std::string &value_)
{
    return zlink::routing_id_t::from_bytes (
      reinterpret_cast<const uint8_t *> (value_.data ()), value_.size ());
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
    // Hold the typed router_socket_t directly so the hot send/recv path
    // skips perf::socket_t's variant visit+if-constexpr indirection.
    // dealer_router_client uses the same approach with dealer_socket_t,
    // and that pattern is the only structural reason RR ratios trailed
    // DR ratios at the same size (cpp.md round 21).
    zlink::router_socket_t *sock;
    std::vector<char> request_buffer;
    size_t payload_size;
    zlink::message_t request;
    zlink::message_t reply;
    // Reusable source routing id scratch for recv_reply. Avoids the
    // per-call routing_id_from_ascii("x") allocation in the hot recv
    // loop that pushed routed-echo recv overhead above the C reference
    // baseline (cpp.md round 21).
    zlink::routing_id_t source_rid_scratch;
    bool awaiting_reply;
    bool send_pending;
    bool pollout_enabled;

    socket_state_t ()
        : sock (NULL),
          request_buffer (),
          payload_size (0),
          request (),
          reply (),
          source_rid_scratch (zlink::routing_id_t::from_bytes (
            reinterpret_cast<const uint8_t *> ("x"), 1)),
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
          _run_id (1U),
          _seq (1),
          _server_id ("SERVER"),
          _server_rid (routing_id_from_ascii (_server_id)),
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
        {
            debug_log ("setup_sockets failed errno=" + std::to_string (errno));
            return false;
        }
        if (!validate_routes_once ())
        {
            debug_log ("validate_routes_once failed errno=" + std::to_string (errno));
            return false;
        }

        _resource_probe_start = perf::multi::start_resource_probe ();
        if (!run_phase (perf_metric::phase_active,
                        _phase_cfg.active_seconds,
                        &_result.active_count,
                        &_result.latency))
        {
            debug_log ("run_phase(active) failed errno=" + std::to_string (errno));
            return false;
        }
        if (_result.active_count == 0)
        {
            debug_log ("active_count stayed zero");
            return false;
        }

        send_stop_token_once ();
        _resource_metrics =
          perf::multi::finish_resource_probe (_resource_probe_start);
        print_result ();
        return true;
    }

  private:
    bool setup_sockets ()
    {
        try {
        for (size_t i = 0; i < _settings.clients; ++i) {
            _holders.emplace_back (
              new zlink::router_socket_t (_ctx.ctx ()));
            zlink::router_socket_t &sock = *_holders.back ();

            const std::string routing_id = std::string ("rr_") + std::to_string (i);
            (void) sock.set_routing_id (
              zlink::routing_id_t::from_bytes (
                reinterpret_cast<const uint8_t *> (routing_id.data ()),
                routing_id.size ()));
            try {
                sock.options ().connect_routing_id (
                  zlink::routing_id_t::from_bytes (
                    reinterpret_cast<const uint8_t *> (_server_id.data ()),
                    _server_id.size ()));
            } catch (const zlink::config_error_t &) {
                return false;
            }

            perf::multi::apply_benchmark_socket_options (sock, _settings, _transport);
            if (!perf::multi::apply_benchmark_auto_hwm_msg_unit_typed (
                  sock, _msg_size))
                return false;
            if (!perf::multi::setup_tls_client (sock, _transport))
                return false;
            _monitors.push_back (perf::multi::connect_monitor_t ());
            if (!perf::multi::open_connect_monitor (sock, _monitors.back ()))
                return false;
            sock.connect (_endpoint);

            socket_state_t state;
            state.sock = &sock;
            _socket_states.push_back (state);
            socket_state_t &slot = _socket_states.back ();
            slot.payload_size =
              std::max<size_t> (_msg_size, perf_metric::header_size ());
            slot.request_buffer.assign (slot.payload_size, k_payload_fill);
            _poller.add (sock, zlink::poll_event_flag_t::pollin, &slot);
        }

        const bool ready = perf::multi::wait_all_connect_ready (
          _monitors, _settings.connect_ready_timeout_ms);
        for (size_t i = 0; i < _monitors.size (); ++i)
            perf::multi::close_connect_monitor (_monitors[i]);
        if (!ready)
        {
            debug_log ("wait_all_connect_ready failed");
            return false;
        }

        return !_socket_states.empty ();
        }
        catch (const zlink::zlink_error_t &) {
            debug_log ("connect failed endpoint=" + _endpoint
                       + " errno=" + std::to_string (errno));
            return false;
        }
    }

    bool validate_routes_once ()
    {
        if (_socket_states.empty ())
            return false;

        try {
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
            // wait_all_into reuses _poll_events' backing storage so the
            // hot poll loop avoids allocating a fresh vector per wake.
            _poller.wait_all_into (
              _poll_events, 0, std::chrono::milliseconds (-1));
            if (_poll_events.empty ())
                continue;

            for (size_t i = 0; i < _poll_events.size (); ++i) {
                socket_state_t *state = static_cast<socket_state_t *> (
                  _poll_events[i].raw_tag);
                if (!state || !state->sock)
                    continue;

                const size_t slot_index =
                  static_cast<size_t> (state - &_socket_states[0]);
                if (slot_index >= validated.size ())
                    return false;

                if ((static_cast<short> (_poll_events[i].revents) & static_cast<short> (zlink::poll_event_flag_t::pollout))
                    && state->send_pending) {
                    if (!try_send_request (*state, perf_metric::phase_warmup))
                        return false;
                }

                if (!(static_cast<short> (_poll_events[i].revents) & static_cast<short> (zlink::poll_event_flag_t::pollin))) {
                    continue;
                }

                for (;;) {
                    perf_metric::header_t header;
                    const int recv_rc =
                      recv_reply (*state, &header);
                    if (recv_rc < 0) {
                        const int err = errno;
                        if (err == EAGAIN)
                            break;
                        if (err == EINTR)
                            continue;
                        debug_log ("validate recv failed errno=" + std::to_string (err));
                        return false;
                    }

                    state->awaiting_reply = false;
                    if (recv_rc != 0) {
                        debug_log ("validate recv ignored rc=" + std::to_string (recv_rc));
                        continue;
                    }
                    if (!perf_metric::is_expected (
                          header, _run_id, perf_metric::phase_warmup, _msg_size)) {
                        debug_log ("validate header mismatch");
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
        catch (const zlink::zlink_error_t &) {
            return false;
        }
    }

    // PERF_MULTI_TEST_POLICY § 1.3.1: pollers wait with timeout=-1
    // (signal-driven). The outer loops keep enforcing the wall-time
    // deadline via steady_clock checks.

    bool set_pollout (socket_state_t &state, bool enabled)
    {
        if (!state.sock)
            return false;
        if (state.pollout_enabled == enabled)
            return true;

        const zlink::poll_event_flag_t events =
          enabled ? (zlink::poll_event_flag_t::pollin | zlink::poll_event_flag_t::pollout)
                  : zlink::poll_event_flag_t::pollin;
        try {
            _poller.modify (*state.sock, events);
            state.pollout_enabled = enabled;
            return true;
        }
        catch (const zlink::zlink_error_t &) {
            return false;
        }
    }

    bool try_send_request (socket_state_t &state, perf_metric::phase_t phase)
    {
        if (!state.sock || state.request_buffer.empty ())
            return false;

        const uint64_t sent_ts_ns = perf_metric::now_ns ();
        if (!perf_metric::stamp_payload (&state.request_buffer[0],
                                         state.payload_size,
                                         _run_id,
                                         phase,
                                         _msg_size,
                                         _seq++,
                                         sent_ts_ns)) {
            return false;
        }

        zlink::message_t request =
          zlink::advanced::external_message_t::adopt (
            state.request_buffer.empty () ? NULL : state.request_buffer.data (),
            state.request_buffer.size (),
            NULL,
            NULL);
        if (!request.valid ()) {
            return false;
        }

        state.request = std::move (request);

        try {
            if (state.sock->send (_server_rid, state.request,
                                  zlink::send_flags_t::dontwait)) {
                state.awaiting_reply = true;
                state.send_pending = false;
                return set_pollout (state, false);
            }
            // dontwait + backpressure → submit() returns false rather than
            // throwing.
            state.send_pending = true;
            errno = EAGAIN;
            return set_pollout (state, true);
        } catch (const zlink::submit_error_t &err) {
            const int err_no = err.internal_errno ();
            if (err_no == EAGAIN || err_no == EWOULDBLOCK) {
                state.send_pending = true;
                errno = err_no;
                return set_pollout (state, true);
            }
            errno = err_no;
            return false;
        }
    }

    int recv_reply (socket_state_t &state,
                    perf_metric::header_t *header_out)
    {
        if (!state.sock || !header_out) {
            errno = EINVAL;
            return -1;
        }

        const int rc = state.sock->recv (
          state.source_rid_scratch, state.reply,
          zlink::recv_flags_t::dontwait);
        if (rc != 0)
            return -1;

        if (!state.reply.valid ()) {
            errno = EPROTO;
            return -1;
        }

        if (state.reply.size () != state.payload_size)
            return 1;
        if (!perf_metric::decode_payload_header (
              state.reply.data (), state.reply.size (), header_out)) {
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

        try {
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
            // wait_all_into reuses _poll_events' backing storage so the
            // hot poll loop avoids allocating a fresh vector per wake.
            _poller.wait_all_into (
              _poll_events, 0, std::chrono::milliseconds (-1));
            if (_poll_events.empty ())
                continue;

            for (size_t i = 0; i < _poll_events.size (); ++i) {
                socket_state_t *state = static_cast<socket_state_t *> (
                  _poll_events[i].raw_tag);
                if (!state || !state->sock)
                    continue;

                if (!(static_cast<short> (_poll_events[i].revents) & static_cast<short> (zlink::poll_event_flag_t::pollin))) {
                    if ((static_cast<short> (_poll_events[i].revents) & static_cast<short> (zlink::poll_event_flag_t::pollout))
                        && state->send_pending) {
                        if (!try_send_request (*state, phase))
                            return false;
                    }
                    continue;
                }

                for (;;) {
                    perf_metric::header_t header;
                    const int recv_rc =
                      recv_reply (*state, &header);
                    if (recv_rc < 0) {
                        const int err = errno;
                        if (err == EAGAIN)
                            break;
                        if (err == EINTR)
                            continue;
                        debug_log ("active recv failed errno=" + std::to_string (err));
                        return false;
                    }
                    state->awaiting_reply = false;

                    if (recv_rc != 0) {
                        debug_log ("active recv ignored rc=" + std::to_string (recv_rc));
                    } else if (perf_metric::is_expected (
                                 header, _run_id, phase, _msg_size)) {
                        ++count;
                        if (lat_out && phase == perf_metric::phase_active) {
                            const uint64_t now_ns = perf_metric::now_ns ();
                            const double latency_ns =
                              now_ns >= header.sent_ts_ns
                                ? static_cast<double> (now_ns - header.sent_ts_ns)
                                    * 0.5
                                : 0.0;
                            latency.add (latency_ns);
                        }
                    } else {
                        debug_log ("active header mismatch");
                    }

                    if (std::chrono::steady_clock::now () >= deadline)
                        continue;

                    if (!state->send_pending) {
                        if (!try_send_request (*state, phase))
                            return false;
                    }
                }

                if ((static_cast<short> (_poll_events[i].revents) & static_cast<short> (zlink::poll_event_flag_t::pollout))
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
        catch (const zlink::zlink_error_t &) {
            return false;
        }
    }

    void send_stop_token_once ()
    {
        if (_socket_states.empty () || !_socket_states[0].sock)
            return;

        zlink::router_socket_t *sock = _socket_states[0].sock;
        const char *stop = perf::multi::k_stop_token;
        const size_t stop_len = std::strlen (stop);
        zlink::message_t stop_msg (stop_len);
        if (!stop_msg.valid ())
            return;
        std::memcpy (stop_msg.data (), stop, stop_len);
        try {
            (void) sock->send (_server_rid, stop_msg,
                               zlink::send_flags_t::dontwait);
        } catch (const zlink::submit_error_t &) {
            // Stop token is best-effort; ignore submit failures.
        }
    }

    void print_result () const
    {
        perf::multi::print_client_result_lines (
          _lib_name,
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
    const std::string _lib_name;
    const size_t _msg_size;
    const std::string _endpoint;
    const perf::multi::multi_bench_settings_t _settings;

    perf::multi::ctx_guard_t _ctx;
    std::vector<std::unique_ptr<zlink::router_socket_t> > _holders;
    std::vector<perf::multi::connect_monitor_t> _monitors;
    std::vector<socket_state_t> _socket_states;
    zlink::poller_t _poller;
    std::vector<zlink::poll_event_t> _poll_events;

    const uint32_t _run_id;
    uint64_t _seq;
    const std::string _server_id;
    zlink::routing_id_t _server_rid;

    phase_config_t _phase_cfg;
    bench_result_t _result;
    bench_multi_cpu_sample_t _resource_probe_start;
    bench_multi_resource_metrics_t _resource_metrics;
};

} // namespace

bool perf_router_router_client (const std::string &lib_name,
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

    router_router_client_bench_t bench (
      transport, lib_name, msg_size, endpoint, settings);
    return bench.run ();
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

    return perf_router_router_client (lib_name, transport, size, endpoint) ? 0
                                                                            : 1;
}
