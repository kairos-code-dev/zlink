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
#include <optional>
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
    bool borrow_payload;
    bool awaiting_reply;
    bool send_pending;
    zlink::poll_event_flag_t poll_events;

    socket_state_t ()
        : sock (NULL),
          request_buffer (),
          payload_size (0),
          request (),
          reply (),
          borrow_payload (false),
          awaiting_reply (false),
          send_pending (false),
          poll_events (zlink::poll_event_flag_t::none)
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
          _shared_request_buffer (),
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

        bool ok = true;
        if (!run_phase (perf_metric::phase_active,
                        _phase_cfg.active_seconds,
                        &_result.active_count,
                        &_result.latency))
        {
            debug_log ("run_phase(active) failed errno=" + std::to_string (errno));
            ok = false;
        }
        if (ok && _result.active_count == 0)
        {
            debug_log ("active_count stayed zero");
            ok = false;
        }

        if (ok) {
            print_result ();
            std::cout.flush ();
        }
        // No stop token: matches the C reference echo client
        // (bindings/c/perf/multi/src/perf_multi_router_router_client.cpp),
        // which runs to its deadline and never signals the relay server.
        // The server is terminated via the run_comparison.py stdin STOP
        // path (and SIGTERM fallback). dotnet removed its equivalent
        // TrySendRouterStopToken for the same reason.
        return ok;
    }

  private:
    bool setup_sockets ()
    {
        try {
        const size_t payload_size =
          std::max<size_t> (_msg_size, perf_metric::header_size ());
        if (_transport != "tcp")
            _shared_request_buffer.assign (payload_size, k_payload_fill);

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
            if (!perf::multi::apply_benchmark_auto_hwm_msg_unit (
                  _ctx, _msg_size))
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
            slot.payload_size = payload_size;
            slot.request_buffer.assign (
              _transport == "tcp" ? payload_size : 0,
              k_payload_fill);
            // Match the C reference: TCP sends can borrow the per-socket
            // stable payload buffer; framed transports keep the owning copy.
            slot.borrow_payload = (_transport == "tcp");
            _poller.add (sock, zlink::poll_event_flag_t::none, &slot);
        }

        const bool ready = perf::multi::wait_connect_ready_all (
          _monitors, _settings.connect_ready_timeout_ms);
        for (size_t i = 0; i < _monitors.size (); ++i)
            perf::multi::close_connect_monitor (_monitors[i]);
        if (!ready)
        {
            debug_log ("wait_connect_ready_all failed");
            return false;
        }
        if (!perf::multi::recalculate_auto_hwm (_ctx))
            return false;
        if (!_holders.empty () && _holders[0].get () && _holders[0]->valid ()) {
            perf::multi::emit_auto_hwm_detail (
              *_holders[0], "client", "endpoint", _transport, _msg_size, "router");
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
            if (!update_poll_interest (state)
                || !try_send_request (state, perf_metric::phase_warmup)) {
                return false;
            }
        }

        while (remaining > 0 && std::chrono::steady_clock::now () < deadline) {
            // wait(events, ...) reuses _poll_events' backing storage so the
            // hot poll loop avoids allocating a fresh vector per wake.
            _poller.wait (
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
                    if (!update_poll_interest (*state))
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

    bool update_poll_interest (socket_state_t &state)
    {
        if (!state.sock)
            return false;

        zlink::poll_event_flag_t events = zlink::poll_event_flag_t::none;
        if (state.awaiting_reply)
            events = zlink::poll_event_flag_t::pollin;
        else if (state.send_pending)
            events = zlink::poll_event_flag_t::pollout;

        if (state.poll_events == events)
            return true;

        try {
            _poller.modify (*state.sock, events);
            state.poll_events = events;
            return true;
        }
        catch (const zlink::zlink_error_t &) {
            return false;
        }
    }

    bool try_send_request (socket_state_t &state, perf_metric::phase_t phase)
    {
        std::vector<char> &request_buffer =
          state.borrow_payload ? state.request_buffer : _shared_request_buffer;
        if (!state.sock || request_buffer.empty ())
            return false;

        const uint64_t sent_ts_ns = perf_metric::now_ns ();
        if (!perf_metric::stamp_payload (&request_buffer[0],
                                         state.payload_size,
                                         _run_id,
                                         phase,
                                         _msg_size,
                                         _seq,
                                         sent_ts_ns)) {
            return false;
        }
        zlink::message_t request =
          state.borrow_payload
            ? zlink::advanced::external_message_t::adopt (
                request_buffer.empty () ? NULL : request_buffer.data (),
                state.payload_size, NULL, NULL)
            : zlink::message_t::from_bytes (
                request_buffer.empty () ? NULL : request_buffer.data (),
                state.payload_size);
        if (!request.valid ()) {
            return false;
        }

        try {
            if (std::move (state.sock->send (_server_rid))
                  .message (request)
                  .flags (zlink::send_flags_t::dontwait)
                  .submit ()) {
                ++_seq;
                state.awaiting_reply = true;
                state.send_pending = false;
                return update_poll_interest (state);
            }
            // dontwait + backpressure returns false rather than throwing.
            state.awaiting_reply = false;
            state.send_pending = true;
            errno = EAGAIN;
            return update_poll_interest (state);
        } catch (const zlink::submit_error_t &err) {
            const int err_no = err.internal_errno ();
            if (err_no == EAGAIN || err_no == EWOULDBLOCK) {
                state.awaiting_reply = false;
                state.send_pending = true;
                errno = err_no;
                return update_poll_interest (state);
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

        state.reply.init ();
        const zlink_routing_id_t *source_node_rid = NULL;
        const zlink_routing_id_t *source_spot_rid = NULL;
        uint64_t request_seq = 0;
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        const int rc = zlink_router_recv_part (
          zlink::detail::native_handle (*state.sock),
          &source_node_rid,
          &source_spot_rid,
          &request_seq,
          zlink::detail::native_handle (state.reply),
          &has_more,
          ZLINK_RECV_FLAGS_DONTWAIT);
        if (rc != 0) {
            state.reply.close ();
            return -1;
        }
        if (!source_node_rid || source_node_rid->size == 0
            || (source_spot_rid && source_spot_rid->size != 0)
            || request_seq != 0 || has_more != ZLINK_PART_FINAL) {
            state.reply.close ();
            errno = EPROTO;
            return -1;
        }

        if (!state.reply.valid ()) {
            errno = EPROTO;
            return -1;
        }

        if (state.reply.size () != state.payload_size) {
            state.reply.close ();
            return 1;
        }
        if (!perf_metric::decode_payload_header (
              state.reply.data (), state.reply.size (), header_out)) {
            state.reply.close ();
            return 1;
        }

        state.reply.close ();
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
        // PERF_MULTI_TEST_POLICY § 1.3.1: round-trip echo window is bounded
        // purely by an application clock (steady_clock deadline) plus a -1
        // (signal-driven) poll wait; no poller timer object is used. Matches
        // the C reference run_echo_window_round_robin
        // (bindings/c/perf/multi/common/perf_multi_client_helpers.hpp:901-1075).
        const auto deadline = std::chrono::steady_clock::now ()
                              + std::chrono::seconds (seconds);

        for (size_t attempt = 0; attempt < _socket_states.size (); ++attempt) {
            socket_state_t &state = _socket_states[attempt];
            if (!state.sock || state.awaiting_reply || state.send_pending)
                continue;
            if (!try_send_request (state, phase))
                return false;
        }

        while (std::chrono::steady_clock::now () < deadline) {
            _poller.wait (_poll_events, _socket_states.size () + 1,
                          std::chrono::milliseconds (-1));
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
                            const double latency_ns =
                              perf_metric::elapsed_latency_ns (
                                perf_metric::now_ns (), header.sent_ts_ns)
                              * 0.5;
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

                    break;
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
          _result.latency);
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
    std::vector<char> _shared_request_buffer;
    std::vector<socket_state_t> _socket_states;
    zlink::poller_t _poller;
    std::vector<zlink::poll_event_t> _poll_events;

    const uint32_t _run_id;
    uint64_t _seq;
    const std::string _server_id;
    zlink::routing_id_t _server_rid;

    phase_config_t _phase_cfg;
    bench_result_t _result;
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
