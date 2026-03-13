// GATEWAY multi client benchmark: service gateway + per-client receiver echo workload.
// Topology: client gateway_t(send, N) + receiver_t(recv, N) <-> server receiver_t(bind, 1) + gateway_t(send)
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
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

static const char *k_pattern_env = "GATEWAY";
static const char *k_pattern_result = "MULTI_GATEWAY";
static const char *k_server_service_name = "perf-server";
static const char *k_client_service_prefix = "c";
static const char k_payload_fill = 'g';
static const int k_ready_timeout_ms = 5000;

bool perf_debug_enabled ()
{
    return std::getenv ("PERF_DEBUG") != NULL;
}

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

bool configure_receiver_tls (zlink::service::receiver_t &receiver,
                             const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    std::string cert;
    std::string key;
    std::string ca;
    if (!perf::multi::try_resolve_perf_tls_paths (cert, key, ca))
        return false;

    (void) ca;
    return receiver.set_tls_server (cert, key) == 0;
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

zlink::poll_event no_events ()
{
    return static_cast<zlink::poll_event> (0);
}

std::string make_client_service_name (size_t index)
{
    return std::string (k_client_service_prefix) + std::to_string (index);
}

bool wait_discovery_ready (zlink::service::discovery_t &discovery,
                           const std::string &service,
                           int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (timeout_ms);
    while (std::chrono::steady_clock::now () < deadline) {
        if (discovery.receiver_count (service) > 0)
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    return discovery.receiver_count (service) > 0;
}

bool wait_gateway_ready (zlink::service::gateway_t &gateway,
                         const std::string &service,
                         int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (timeout_ms);
    while (std::chrono::steady_clock::now () < deadline) {
        if (gateway.connection_count (service) > 0)
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    return gateway.connection_count (service) > 0;
}

std::string bind_receiver_endpoint (zlink::service::receiver_t &receiver,
                                    const std::string &transport,
                                    const std::string &id)
{
    const std::string endpoint = perf::multi::make_endpoint (transport, id, 0);
    if (receiver.bind (endpoint) != 0)
        return std::string ();

    char last_endpoint[256] = "";
    size_t size = sizeof (last_endpoint);
    std::string resolved = endpoint;
    if (zlink_receiver_last_endpoint (receiver.handle (), last_endpoint, &size) == 0)
        resolved.assign (last_endpoint);

    return perf::multi::normalize_endpoint_host (resolved);
}

long compute_wait_ms (const std::chrono::steady_clock::time_point &deadline,
                      int fallback_poll_timeout_ms)
{
    const auto now = std::chrono::steady_clock::now ();
    if (now >= deadline)
        return 1;

    long wait_ms = fallback_poll_timeout_ms > 0 ? fallback_poll_timeout_ms : 100;
    const long remain_ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                             deadline - now)
                             .count ();
    if (remain_ms < wait_ms)
        wait_ms = remain_ms;
    if (wait_ms < 1)
        wait_ms = 1;
    return wait_ms;
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

struct slot_state_t
{
    zlink::service::gateway_t *gateway;
    zlink::service::receiver_t *receiver;
    std::string service_name;
    std::vector<char> payload;
    std::vector<char> recv_buffer;
    bool awaiting_reply;
    bool send_pending;
    bool pollout_enabled;

    slot_state_t ()
        : gateway (NULL),
          receiver (NULL),
          service_name (),
          payload (),
          recv_buffer (),
          awaiting_reply (false),
          send_pending (false),
          pollout_enabled (false)
    {
    }
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
          _registry_pub_endpoint (),
          _registry_router_endpoint (),
          _server_endpoint (),
          _discovery (_ctx.ctx (), zlink::service_type::gateway),
          _receiver_holders (),
          _gateway_holders (),
          _states (),
          _receiver_poller (),
          _gateway_poller (),
          _receiver_events (),
          _gateway_events (),
          _run_id (static_cast<uint32_t> (perf_metric::now_us ())),
          _seq (1),
          _phase_cfg (),
          _result ()
    {
        _receiver_holders.reserve (_settings.clients);
        _gateway_holders.reserve (_settings.clients);
        _states.reserve (_settings.clients);
        _receiver_events.reserve (_settings.clients);
        _gateway_events.reserve (_settings.clients);

        _phase_cfg.warmup_seconds = std::max (0, _settings.warmup_seconds);
        _phase_cfg.settle_ms = std::max (0, _settings.settle_ms);
        _phase_cfg.active_seconds = std::max (1, _settings.duration_seconds);
    }

    bool run ()
    {
        if (!parse_ready_payload (_endpoint,
                                  &_server_endpoint,
                                  &_registry_pub_endpoint,
                                  &_registry_router_endpoint)) {
            if (perf_debug_enabled ())
                std::cerr << "gateway client: parse ready payload failed: "
                          << _endpoint << std::endl;
            return false;
        }
        if (!setup_discovery ()) {
            if (perf_debug_enabled ())
                std::cerr << "gateway client: setup discovery failed" << std::endl;
            return false;
        }
        if (!setup_slots ()) {
            if (perf_debug_enabled ())
                std::cerr << "gateway client: setup slots failed" << std::endl;
            return false;
        }
        if (!prime_roundtrips ()) {
            if (perf_debug_enabled ())
                std::cerr << "gateway client: prime roundtrips failed" << std::endl;
            return false;
        }

        perf::multi::settle ();

        if (!run_warmup ()) {
            if (perf_debug_enabled ())
                std::cerr << "gateway client: warmup failed" << std::endl;
            return false;
        }
        if (!run_settle ()) {
            if (perf_debug_enabled ())
                std::cerr << "gateway client: settle failed" << std::endl;
            return false;
        }
        if (!run_active ()) {
            if (perf_debug_enabled ())
                std::cerr << "gateway client: active failed" << std::endl;
            return false;
        }
        if (_result.active_count == 0) {
            if (perf_debug_enabled ())
                std::cerr << "gateway client: no active messages" << std::endl;
            return false;
        }

        send_stop_token_once ();
        print_result ();
        return true;
    }

  private:
    bool setup_discovery ()
    {
        if (!_discovery.valid ()
            || _discovery.connect_registry (_registry_router_endpoint) != 0) {
            if (perf_debug_enabled ()) {
                std::cerr << "gateway client: discovery connect failed registry="
                          << _registry_router_endpoint
                          << " err=" << zlink::last_error ().what () << std::endl;
            }
            return false;
        }

        return wait_discovery_ready (_discovery,
                                     k_server_service_name,
                                     std::max (_settings.connect_ready_timeout_ms,
                                               k_ready_timeout_ms));
    }

    bool setup_slots ()
    {
        const size_t payload_size =
          std::max<size_t> (_msg_size, perf_metric::header_size ());
        const int ready_timeout_ms =
          std::max (_settings.connect_ready_timeout_ms, k_ready_timeout_ms);

        for (size_t i = 0; i < _settings.clients; ++i) {
            const std::string service_name = make_client_service_name (i);

            _receiver_holders.emplace_back (
              new zlink::service::receiver_t (_ctx.ctx (), service_name));
            zlink::service::receiver_t &receiver = *_receiver_holders.back ();
            if (!receiver.valid ()) {
                if (perf_debug_enabled ())
                    std::cerr << "gateway client: receiver create failed service="
                              << service_name << std::endl;
                return false;
            }

            (void) receiver.set_sockopt (zlink::receiver_socket_role::router,
                                         zlink::socket_options::sndhwm,
                                         _settings.sndhwm);
            (void) receiver.set_sockopt (zlink::receiver_socket_role::router,
                                         zlink::socket_options::rcvhwm,
                                         _settings.rcvhwm);
            (void) receiver.set_sockopt (zlink::receiver_socket_role::router,
                                         zlink::socket_options::sndtimeo,
                                         _settings.sndtimeo_ms);
            (void) receiver.set_sockopt (zlink::receiver_socket_role::router,
                                         zlink::socket_options::rcvtimeo,
                                         _settings.rcvtimeo_ms);

            if (!configure_receiver_tls (receiver, _transport)) {
                if (perf_debug_enabled ())
                    std::cerr << "gateway client: receiver tls configure failed service="
                              << service_name << std::endl;
                return false;
            }

            const std::string receiver_endpoint =
              bind_receiver_endpoint (receiver,
                                      _transport,
                                      std::string ("cpp_multi_gateway_client_")
                                        + std::to_string (i));
            if (receiver_endpoint.empty ()) {
                if (perf_debug_enabled ())
                    std::cerr << "gateway client: receiver bind failed service="
                              << service_name << std::endl;
                return false;
            }
            if (receiver.connect_registry (_registry_router_endpoint) != 0
                || receiver.register_service (service_name, receiver_endpoint, 1) != 0) {
                if (perf_debug_enabled ()) {
                    std::cerr << "gateway client: receiver register failed service="
                              << service_name << " endpoint=" << receiver_endpoint
                              << " registry=" << _registry_router_endpoint
                              << " err=" << zlink::last_error ().what () << std::endl;
                }
                return false;
            }

            _gateway_holders.emplace_back (
              new zlink::service::gateway_t (_ctx.ctx (), _discovery, service_name));
            zlink::service::gateway_t &gateway = *_gateway_holders.back ();
            if (!gateway.valid ()) {
                if (perf_debug_enabled ())
                    std::cerr << "gateway client: gateway create failed service="
                              << service_name << std::endl;
                return false;
            }

            (void) gateway.set_sockopt (zlink::socket_options::sndhwm,
                                        _settings.sndhwm);
            (void) gateway.set_sockopt (zlink::socket_options::rcvhwm,
                                        _settings.rcvhwm);
            (void) gateway.set_sockopt (zlink::socket_options::sndtimeo,
                                        _settings.sndtimeo_ms);
            (void) gateway.set_sockopt (zlink::socket_options::rcvtimeo,
                                        _settings.rcvtimeo_ms);

            if (!configure_gateway_tls (gateway, _transport)) {
                if (perf_debug_enabled ())
                    std::cerr << "gateway client: gateway tls configure failed service="
                              << service_name << std::endl;
                return false;
            }
            if (!wait_gateway_ready (gateway, k_server_service_name, ready_timeout_ms)) {
                if (perf_debug_enabled ())
                    std::cerr << "gateway client: gateway ready wait failed service="
                              << service_name << std::endl;
                return false;
            }

            _states.emplace_back ();
            slot_state_t &state = _states.back ();
            state.gateway = &gateway;
            state.receiver = &receiver;
            state.service_name = service_name;
            state.payload.assign (payload_size, k_payload_fill);
            state.recv_buffer.assign (payload_size, '\0');

            if (_receiver_poller.add_receiver (receiver,
                                              zlink::poll_event::pollin,
                                              &state)
                != 0) {
                return false;
            }
            if (_gateway_poller.add_gateway (gateway,
                                             no_events (),
                                             &state)
                != 0) {
                return false;
            }
        }

        return !_states.empty ();
    }

    bool set_gateway_pollout (slot_state_t &state, bool enabled)
    {
        if (!state.gateway)
            return false;
        if (state.pollout_enabled == enabled)
            return true;

        const zlink::poll_event events =
          enabled ? zlink::poll_event::pollout : no_events ();
        if (_gateway_poller.modify_gateway (*state.gateway, events) != 0)
            return false;
        state.pollout_enabled = enabled;
        return true;
    }

    bool try_send_request (slot_state_t &state, perf_metric::phase_t phase)
    {
        if (!state.gateway)
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
            if (phase == perf_metric::phase_active) {
                perf::multi::debug_header_trace ("client",
                                                k_pattern_result,
                                                _transport,
                                                _msg_size,
                                                "send",
                                                static_cast<uint32_t> (phase),
                                                _run_id,
                                                _seq - 1,
                                                sent_ts,
                                                sent_ts,
                                                -1.0);
            }
        }

        if (state.gateway->send (k_server_service_name,
                                 state.payload.data (),
                                 state.payload.size (),
                                 zlink::send_flag::dontwait)
            == 0) {
            state.awaiting_reply = true;
            state.send_pending = false;
            return set_gateway_pollout (state, false);
        }

        if (errno == EAGAIN) {
            state.send_pending = true;
            return set_gateway_pollout (state, true);
        }

        return false;
    }

    bool recv_reply_payload (slot_state_t &state,
                          size_t *received_size_out,
                          bool *idle_out)
    {
        if (!state.receiver || state.recv_buffer.empty () || !received_size_out)
            return false;
        if (idle_out)
            *idle_out = false;
        *received_size_out = 0;

        for (;;) {
            size_t received = 0;
            if (state.receiver->recv (state.recv_buffer.data (),
                                      state.recv_buffer.size (),
                                      &received,
                                      zlink::recv_flag::dontwait)
                == 0) {
                *received_size_out = received;
                return true;
            }

            if (errno == EAGAIN) {
                if (idle_out)
                    *idle_out = true;
                return true;
            }
            if (errno == EINTR)
                continue;
            return false;
        }
    }

    bool drain_receiver (slot_state_t &state,
                         perf_metric::phase_t phase,
                         unsigned long long *count,
                         perf::multi::bench_latency_sampler_t *lat_out,
                         bool *progress_out)
    {
        if (progress_out)
            *progress_out = false;

        for (;;) {
            size_t payload_size = 0;
            bool idle = false;
            if (!recv_reply_payload (state, &payload_size, &idle))
                return false;
            if (idle)
                return true;

            if (progress_out)
                *progress_out = true;
            state.awaiting_reply = false;

            perf_metric::header_t header;
            if (!perf_metric::decode_payload_header (state.recv_buffer.data (),
                                                     payload_size,
                                                     &header)) {
                continue;
            }
            if (!perf_metric::is_expected (header, _run_id, phase, _msg_size)) {
                continue;
            }

            if (count)
                ++(*count);
            if (lat_out && phase == perf_metric::phase_active) {
                const uint64_t now_us = perf_metric::now_us ();
                const double latency_us =
                  now_us >= header.sent_ts_us
                    ? static_cast<double> (now_us - header.sent_ts_us) * 0.5
                    : 0.0;
                perf::multi::debug_header_trace ("client",
                                                k_pattern_result,
                                                _transport,
                                                _msg_size,
                                                "recv",
                                                header.phase,
                                                header.run_id,
                                                header.seq,
                                                header.sent_ts_us,
                                                now_us,
                                                latency_us);
                lat_out->add (latency_us);
            }
        }
    }

    bool pump_receiver_events (perf_metric::phase_t phase,
                               unsigned long long *count,
                               perf::multi::bench_latency_sampler_t *lat_out,
                               size_t *completed_slots,
                               std::vector<unsigned char> *seen)
    {
        for (size_t i = 0; i < _receiver_events.size (); ++i) {
            slot_state_t *state =
              static_cast<slot_state_t *> (_receiver_events[i].user);
            if (!state || !state->receiver)
                continue;
            if (!(_receiver_events[i].revents
                  & static_cast<short> (zlink::poll_event::pollin))) {
                continue;
            }

            bool progressed = false;
            const unsigned long long before = count ? *count : 0;
            if (!drain_receiver (*state, phase, count, lat_out, &progressed))
                return false;
            if (seen && progressed) {
                const size_t slot_index = static_cast<size_t> (state - &_states[0]);
                if (slot_index < seen->size () && (*seen)[slot_index] == 0) {
                    (*seen)[slot_index] = 1;
                    if (completed_slots)
                        ++(*completed_slots);
                }
            } else if (completed_slots && count && *count > before) {
                ++(*completed_slots);
            }
        }
        return true;
    }

    bool pump_gateway_pollout (perf_metric::phase_t phase)
    {
        for (size_t i = 0; i < _gateway_events.size (); ++i) {
            slot_state_t *state =
              static_cast<slot_state_t *> (_gateway_events[i].user);
            if (!state || !state->gateway || !state->send_pending)
                continue;
            if (!(_gateway_events[i].revents
                  & static_cast<short> (zlink::poll_event::pollout))) {
                continue;
            }
            if (!try_send_request (*state, phase))
                return false;
        }
        return true;
    }

    bool prime_roundtrips ()
    {
        if (_states.empty ())
            return false;

        std::vector<unsigned char> primed (_states.size (), 0);
        size_t primed_count = 0;
        size_t send_index = 0;
        const auto deadline = std::chrono::steady_clock::now ()
                              + std::chrono::milliseconds (
                                std::max (_settings.connect_ready_timeout_ms,
                                          k_ready_timeout_ms));

        while (primed_count < _states.size ()
               && std::chrono::steady_clock::now () < deadline) {
            for (size_t i = 0; i < _states.size (); ++i) {
                slot_state_t &state = _states[(send_index + i) % _states.size ()];
                const size_t slot_index = static_cast<size_t> (&state - &_states[0]);
                if (primed[slot_index] != 0 || state.awaiting_reply
                    || state.send_pending) {
                    continue;
                }
                if (!try_send_request (state, perf_metric::phase_drain))
                    return false;
            }
            ++send_index;

            const int recv_poll_rc = _receiver_poller.wait (
              _receiver_events,
              compute_wait_ms (deadline, _settings.client_poll_timeout_ms));
            if (recv_poll_rc < 0) {
                if (errno == EINTR)
                    continue;
                return false;
            }
            if (recv_poll_rc > 0
                && !pump_receiver_events (perf_metric::phase_drain,
                                          NULL,
                                          NULL,
                                          &primed_count,
                                          &primed)) {
                return false;
            }

            const int send_poll_rc = _gateway_poller.wait (_gateway_events, 0);
            if (send_poll_rc < 0) {
                if (errno == EINTR)
                    continue;
                return false;
            }
            if (send_poll_rc > 0
                && !pump_gateway_pollout (perf_metric::phase_drain)) {
                return false;
            }
        }

        return primed_count == _states.size ();
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
        if (_states.empty ())
            return false;

        unsigned long long count = 0;
        size_t send_index = 0;
        const auto deadline = std::chrono::steady_clock::now ()
                              + std::chrono::seconds (seconds);

        while (std::chrono::steady_clock::now () < deadline) {
            for (size_t i = 0; i < _states.size (); ++i) {
                slot_state_t &state = _states[(send_index + i) % _states.size ()];
                if (state.awaiting_reply || state.send_pending)
                    continue;
                if (!try_send_request (state, phase))
                    return false;
            }
            ++send_index;

            const int recv_poll_rc = _receiver_poller.wait (
              _receiver_events,
              compute_wait_ms (deadline, _settings.client_poll_timeout_ms));
            if (recv_poll_rc < 0) {
                if (errno == EINTR)
                    continue;
                return false;
            }
            if (recv_poll_rc > 0
                && !pump_receiver_events (phase, &count, lat_out, NULL, NULL)) {
                return false;
            }

            const int send_poll_rc = _gateway_poller.wait (_gateway_events, 0);
            if (send_poll_rc < 0) {
                if (errno == EINTR)
                    continue;
                return false;
            }
            if (send_poll_rc > 0 && !pump_gateway_pollout (phase))
                return false;
        }

        *count_out = count;
        return true;
    }

    bool run_settle ()
    {
        if (_phase_cfg.settle_ms <= 0 || _states.empty ())
            return true;

        const auto deadline = std::chrono::steady_clock::now ()
                              + std::chrono::milliseconds (_phase_cfg.settle_ms);
        while (std::chrono::steady_clock::now () < deadline) {
            const int recv_poll_rc = _receiver_poller.wait (
              _receiver_events,
              compute_wait_ms (deadline, _settings.client_poll_timeout_ms));
            if (recv_poll_rc < 0) {
                if (errno == EINTR)
                    continue;
                return false;
            }
            if (recv_poll_rc > 0
                && !pump_receiver_events (perf_metric::phase_warmup,
                                          NULL,
                                          NULL,
                                          NULL,
                                          NULL)) {
                return false;
            }

            const int send_poll_rc = _gateway_poller.wait (_gateway_events, 0);
            if (send_poll_rc < 0) {
                if (errno == EINTR)
                    continue;
                return false;
            }
            if (send_poll_rc > 0
                && !pump_gateway_pollout (perf_metric::phase_warmup)) {
                return false;
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
        if (_states.empty () || !_states[0].gateway)
            return;

        const char *stop = perf::multi::k_stop_token;
        const size_t stop_len = std::strlen (stop);
        (void) _states[0].gateway->send (
          k_server_service_name, stop, stop_len, zlink::send_flag::none);
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
    std::string _registry_pub_endpoint;
    std::string _registry_router_endpoint;
    std::string _server_endpoint;
    zlink::service::discovery_t _discovery;
    std::vector<std::unique_ptr<zlink::service::receiver_t> > _receiver_holders;
    std::vector<std::unique_ptr<zlink::service::gateway_t> > _gateway_holders;
    std::vector<slot_state_t> _states;
    zlink::poller_t _receiver_poller;
    zlink::poller_t _gateway_poller;
    std::vector<zlink::poll_event_t> _receiver_events;
    std::vector<zlink::poll_event_t> _gateway_events;

    const uint32_t _run_id;
    uint64_t _seq;

    phase_config_t _phase_cfg;
    bench_result_t _result;
};

} // namespace

bool perf_gateway_client (const std::string &transport,
                          size_t msg_size,
                          const std::string &endpoint)
{
    perf::multi::set_perf_pattern_env (k_pattern_env);

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << k_pattern_result << "," << transport
                  << std::endl;
        return true;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();

    gateway_client_bench_t bench (transport, msg_size, endpoint, settings);
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

    return perf_gateway_client (transport, size, endpoint) ? 0 : 1;
}
