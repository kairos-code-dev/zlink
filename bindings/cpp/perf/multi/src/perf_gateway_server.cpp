// GATEWAY multi server benchmark: receiver + gateway relay echo responder.
// Topology: client gateway_t(send, N) + receiver_t(recv, N) <-> server receiver_t(bind, 1) + gateway_t(send)
// Measurement role: receive requests on server receiver and relay payload back through server gateway.

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

static const char *k_pattern_env = "GATEWAY";
static const char *k_pattern_result = "MULTI_GATEWAY";
static const char *k_server_service_name = "perf-server";
static const char *k_client_service_prefix = "c";
static const char *k_server_gateway_routing_id = "sg";
static const int k_ready_timeout_ms = 5000;

int bench_pid ()
{
#if defined(_WIN32)
    return _getpid ();
#else
    return getpid ();
#endif
}

std::string make_tcp_endpoint (int port)
{
    return std::string ("tcp://127.0.0.1:") + std::to_string (port);
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

bool parse_client_slot (const void *data,
                        size_t size,
                        size_t client_count,
                        size_t *slot_out)
{
    if (!data || size < 2 || !slot_out)
        return false;

    const char *text = static_cast<const char *> (data);
    if (text[0] != k_client_service_prefix[0])
        return false;

    size_t value = 0;
    for (size_t i = 1; i < size; ++i) {
        const char ch = text[i];
        if (ch < '0' || ch > '9')
            return false;
        value = value * 10 + static_cast<size_t> (ch - '0');
        if (value >= client_count)
            return false;
    }

    *slot_out = value;
    return true;
}

bool wait_client_services_ready (zlink::service::discovery_t &discovery,
                                 zlink::service::gateway_t &gateway,
                                 size_t client_count,
                                 int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (timeout_ms);
    while (std::chrono::steady_clock::now () < deadline) {
        bool ready = true;
        for (size_t i = 0; i < client_count; ++i) {
            const std::string service =
              std::string (k_client_service_prefix) + std::to_string (i);
            if (discovery.receiver_count (service) <= 0
                || gateway.connection_count (service) <= 0) {
                ready = false;
                break;
            }
        }
        if (ready)
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }

    for (size_t i = 0; i < client_count; ++i) {
        const std::string service =
          std::string (k_client_service_prefix) + std::to_string (i);
        if (discovery.receiver_count (service) <= 0
            || gateway.connection_count (service) <= 0) {
            return false;
        }
    }
    return true;
}

long compute_wait_ms (const std::chrono::steady_clock::time_point &deadline)
{
    const auto now = std::chrono::steady_clock::now ();
    if (now >= deadline)
        return 1;

    long wait_ms = 100;
    const long remain_ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                             deadline - now)
                             .count ();
    if (remain_ms < wait_ms)
        wait_ms = remain_ms;
    if (wait_ms < 1)
        wait_ms = 1;
    return wait_ms;
}

std::string bind_receiver_endpoint (zlink::service::receiver_t &receiver,
                                    const std::string &transport,
                                    const std::string &id,
                                    int fixed_port)
{
    const std::string endpoint =
      perf::multi::make_endpoint (transport, id, fixed_port);
    if (receiver.bind (endpoint) != 0)
        return std::string ();

    zlink::socket_t router = zlink::socket_t::wrap (receiver.router_handle ());
    std::string last;
    if (router.get (zlink::socket_options::last_endpoint, last) != 0)
        return std::string ();

    return perf::multi::normalize_endpoint_host (last);
}

struct pending_reply_t
{
    size_t slot_index;
    zlink::message_t payload;
    bool has_payload;

    pending_reply_t () : slot_index (0), payload (), has_payload (false) {}
};

class gateway_server_bench_t
{
  public:
    gateway_server_bench_t (const std::string &transport,
                            const perf::multi::multi_bench_settings_t &settings)
        : _transport (transport),
          _settings (settings),
          _ctx (),
          _registry (),
          _receiver (),
          _discovery (),
          _gateway (),
          _receiver_router (),
          _poller (),
          _events (),
          _pending (),
          _client_services (),
          _registry_pub_endpoint (),
          _registry_router_endpoint (),
          _receiver_endpoint ()
    {
        _events.reserve (2);
        const size_t pending_capacity =
          std::max<size_t> (64,
                            std::max<size_t> (_settings.clients,
                                              static_cast<size_t> (
                                                std::max (_settings.sndhwm,
                                                          _settings.rcvhwm)))
                              * 2);
        _pending.reserve (pending_capacity);
        _client_services.reserve (_settings.clients);
        for (size_t i = 0; i < _settings.clients; ++i)
            _client_services.push_back (
              std::string (k_client_service_prefix) + std::to_string (i));
    }

    bool run ()
    {
        if (!setup_registry ())
            return false;
        if (!setup_receiver ())
            return false;
        if (!setup_discovery_and_gateway ())
            return false;

        std::cout << "READY," << _receiver_endpoint << "|" << _registry_pub_endpoint
                  << "|" << _registry_router_endpoint << std::endl;

        if (!wait_client_services_ready (*_discovery,
                                         *_gateway,
                                         _settings.clients,
                                         std::max (_settings.connect_ready_timeout_ms,
                                                   k_ready_timeout_ms))) {
            return false;
        }

        const int warmup_seconds =
          _settings.warmup_seconds > 0 ? _settings.warmup_seconds : 0;
        const int active_seconds =
          _settings.duration_seconds > 0 ? _settings.duration_seconds : 1;
        const int settle_seconds =
          _settings.settle_ms > 0 ? (_settings.settle_ms + 999) / 1000 : 0;
        const auto deadline = std::chrono::steady_clock::now ()
                              + std::chrono::seconds (
                                warmup_seconds + settle_seconds + active_seconds + 5);

        if (_poller.add_receiver (*_receiver, zlink::poll_event::pollin) != 0)
            return false;
        if (_poller.add_gateway (*_gateway, zlink::poll_event::pollin) != 0)
            return false;

        bool stop_requested = false;
        while (!stop_requested && std::chrono::steady_clock::now () < deadline) {
            if (!flush_pending ())
                return false;
            if (!set_gateway_pollout (!_pending.empty ()))
                return false;

            const int poll_rc = _poller.wait (_events, compute_wait_ms (deadline));
            if (poll_rc < 0) {
                if (errno == EINTR)
                    continue;
                return false;
            }
            if (poll_rc == 0)
                continue;

            for (size_t i = 0; i < _events.size () && !stop_requested; ++i) {
                if ((_events[i].revents
                     & static_cast<short> (zlink::poll_event::pollout))
                    && !flush_pending ()) {
                    return false;
                }

                if (!(_events[i].revents
                      & static_cast<short> (zlink::poll_event::pollin))) {
                    continue;
                }

                for (;;) {
                    size_t slot_index = 0;
                    zlink::message_t payload;
                    int recv_status = recv_request (&slot_index, &payload);
                    if (recv_status < 0)
                        return false;
                    if (recv_status == 0)
                        break;

                    if (perf::multi::is_stop_token (payload.data (), payload.size ())) {
                        stop_requested = true;
                        break;
                    }

                    const int send_rc = try_send_reply (slot_index, payload);
                    if (send_rc < 0)
                        return false;
                    if (send_rc == 0)
                        continue;
                    if (!enqueue_pending (slot_index, payload))
                        return false;
                    if (!set_gateway_pollout (true))
                        return false;
                }
            }
        }

        perf::multi::print_server_queue_metrics (
          "current", k_pattern_result, _transport, 0,
          perf::multi::server_queue_stats_t ());
        return true;
    }

  private:
    bool setup_registry ()
    {
        const int base_seed = _settings.server_bind_port > 0
                                ? _settings.server_bind_port + 64
                                : 39064 + (bench_pid () % 1000) * 8;
        const int max_bind_tries = _settings.server_bind_port > 0 ? 1 : 64;

        for (int i = 0; i < max_bind_tries; ++i) {
            const int base_port = _settings.server_bind_port > 0
                                    ? _settings.server_bind_port + 64
                                    : base_seed + i * 8;
            const std::string pub_endpoint = make_tcp_endpoint (base_port);
            const std::string router_endpoint = make_tcp_endpoint (base_port + 1);

            _registry.reset (new zlink::service::registry_t (_ctx.ctx ()));
            if (!_registry->valid ())
                return false;
            if (_registry->set_endpoints (pub_endpoint, router_endpoint) != 0
                || _registry->start () != 0) {
                _registry.reset ();
                continue;
            }

            _registry_pub_endpoint = pub_endpoint;
            _registry_router_endpoint = router_endpoint;
            return true;
        }

        return false;
    }

    bool setup_receiver ()
    {
        const int base_seed = _settings.server_bind_port > 0
                                ? _settings.server_bind_port
                                : 39000 + (bench_pid () % 1000) * 8;
        const int max_bind_tries = _settings.server_bind_port > 0 ? 1 : 64;

        for (int i = 0; i < max_bind_tries; ++i) {
            const int bind_port = _settings.server_bind_port > 0
                                    ? _settings.server_bind_port
                                    : base_seed + i * 8;
            _receiver.reset (new zlink::service::receiver_t (_ctx.ctx (),
                                                             "perf-server-rx"));
            if (!_receiver->valid ())
                return false;

            (void) _receiver->set_sockopt (zlink::receiver_socket_role::router,
                                           zlink::socket_options::sndhwm,
                                           _settings.sndhwm);
            (void) _receiver->set_sockopt (zlink::receiver_socket_role::router,
                                           zlink::socket_options::rcvhwm,
                                           _settings.rcvhwm);
            (void) _receiver->set_sockopt (zlink::receiver_socket_role::router,
                                           zlink::socket_options::sndtimeo,
                                           _settings.sndtimeo_ms);
            (void) _receiver->set_sockopt (zlink::receiver_socket_role::router,
                                           zlink::socket_options::rcvtimeo,
                                           _settings.rcvtimeo_ms);

            if (!configure_receiver_tls (*_receiver, _transport))
                return false;

            _receiver_endpoint = bind_receiver_endpoint (
              *_receiver, _transport, "cpp_multi_gateway_server", bind_port);
            if (_receiver_endpoint.empty ()) {
                _receiver.reset ();
                continue;
            }
            if (_receiver->connect_registry (_registry_router_endpoint) != 0
                || _receiver->register_service (
                  k_server_service_name, _receiver_endpoint, 1)
                     != 0) {
                _receiver.reset ();
                continue;
            }

            _receiver_router.reset (
              new zlink::socket_t (zlink::socket_t::wrap (_receiver->router_handle ())));
            perf::multi::apply_benchmark_socket_options (
              *_receiver_router, _settings, _transport);
            return true;
        }

        return false;
    }

    bool setup_discovery_and_gateway ()
    {
        _discovery.reset (
          new zlink::service::discovery_t (_ctx.ctx (), zlink::service_type::gateway));
        if (!_discovery->valid ())
            return false;
        if (_discovery->connect_registry (_registry_pub_endpoint) != 0)
            return false;

        _gateway.reset (
          new zlink::service::gateway_t (_ctx.ctx (),
                                         *_discovery,
                                         k_server_gateway_routing_id));
        if (!_gateway->valid ())
            return false;

        (void) _gateway->set_sockopt (zlink::socket_options::sndhwm,
                                      _settings.sndhwm);
        (void) _gateway->set_sockopt (zlink::socket_options::rcvhwm,
                                      _settings.rcvhwm);
        (void) _gateway->set_sockopt (zlink::socket_options::sndtimeo,
                                      _settings.sndtimeo_ms);
        (void) _gateway->set_sockopt (zlink::socket_options::rcvtimeo,
                                      _settings.rcvtimeo_ms);

        return configure_gateway_tls (*_gateway, _transport);
    }

    bool set_gateway_pollout (bool enabled)
    {
        const zlink::poll_event events =
          enabled ? (zlink::poll_event::pollin | zlink::poll_event::pollout)
                  : zlink::poll_event::pollin;
        return _poller.modify_gateway (*_gateway, events) == 0;
    }

    bool recv_last_frame_after_first (zlink::message_t *payload_out)
    {
        if (!payload_out)
            return false;

        zlink::message_t frame;
        if (_receiver_router->recv (frame, zlink::recv_flag::dontwait) < 0) {
            if (errno == EINTR)
                return recv_last_frame_after_first (payload_out);
            return false;
        }

        *payload_out = std::move (frame);
        while (payload_out->more ()) {
            zlink::message_t next;
            if (_receiver_router->recv (next, zlink::recv_flag::dontwait) < 0) {
                if (errno == EINTR)
                    continue;
                errno = EPROTO;
                return false;
            }
            *payload_out = std::move (next);
        }
        return true;
    }

    int recv_request (size_t *slot_out, zlink::message_t *payload_out)
    {
        if (!slot_out || !payload_out)
            return -1;

        zlink::message_t client_service;
        if (_receiver_router->recv (client_service, zlink::recv_flag::dontwait) < 0) {
            if (errno == EAGAIN)
                return 0;
            if (errno == EINTR)
                return recv_request (slot_out, payload_out);
            return -1;
        }

        if (!client_service.more ()) {
            errno = EPROTO;
            return -1;
        }
        if (!parse_client_slot (client_service.data (),
                                client_service.size (),
                                _client_services.size (),
                                slot_out)) {
            errno = EPROTO;
            return -1;
        }
        if (!recv_last_frame_after_first (payload_out))
            return -1;
        return 1;
    }

    int try_send_reply (size_t slot_index, const zlink::message_t &payload)
    {
        if (slot_index >= _client_services.size ()) {
            errno = EINVAL;
            return -1;
        }
        if (_gateway->send (_client_services[slot_index],
                            payload.data (),
                            payload.size (),
                            zlink::send_flag::dontwait)
            == 0) {
            return 0;
        }
        if (errno == EAGAIN)
            return 1;
        return -1;
    }

    bool enqueue_pending (size_t slot_index, zlink::message_t &payload)
    {
        if (_pending.size () == _pending.capacity ()) {
            errno = ENOBUFS;
            return false;
        }

        pending_reply_t item;
        item.slot_index = slot_index;
        item.payload = std::move (payload);
        item.has_payload = true;
        _pending.push_back (std::move (item));
        return true;
    }

    bool flush_pending ()
    {
        size_t index = 0;
        while (index < _pending.size ()) {
            pending_reply_t &item = _pending[index];
            const int send_rc = try_send_reply (item.slot_index, item.payload);
            if (send_rc < 0)
                return false;
            if (send_rc == 0) {
                _pending[index] = std::move (_pending.back ());
                _pending.pop_back ();
                continue;
            }
            ++index;
        }
        return true;
    }

    const std::string _transport;
    const perf::multi::multi_bench_settings_t _settings;

    perf::multi::ctx_guard_t _ctx;
    std::unique_ptr<zlink::service::registry_t> _registry;
    std::unique_ptr<zlink::service::receiver_t> _receiver;
    std::unique_ptr<zlink::service::discovery_t> _discovery;
    std::unique_ptr<zlink::service::gateway_t> _gateway;
    std::unique_ptr<zlink::socket_t> _receiver_router;
    zlink::poller_t _poller;
    std::vector<zlink::poll_event_t> _events;
    std::vector<pending_reply_t> _pending;
    std::vector<std::string> _client_services;
    std::string _registry_pub_endpoint;
    std::string _registry_router_endpoint;
    std::string _receiver_endpoint;
};

} // namespace

bool perf_gateway_server (const std::string &transport, size_t)
{
    perf::multi::set_perf_pattern_env (k_pattern_env);

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << k_pattern_result << "," << transport
                  << std::endl;
        return true;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();

    gateway_server_bench_t bench (transport, settings);
    return bench.run ();
}

int main (int argc, char **argv)
{
    if (argc < 3) {
        std::cerr << "usage: <transport> <size>" << std::endl;
        return 1;
    }

    const std::string transport = argv[1];
    const size_t size = static_cast<size_t> (std::strtoull (argv[2], NULL, 10));
    if (size == 0)
        return 1;

    return perf_gateway_server (transport, size) ? 0 : 1;
}
