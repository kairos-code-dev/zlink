// GATEWAY benchmark: one-way gateway->receiver service message flow.
// Topology: gateway(service client) -> receiver(service provider router)

#include "../common/perf_single_common.hpp"
#include "../common/perf_single_runner.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

int bench_pid ()
{
#if defined(_WIN32)
    return _getpid ();
#else
    return getpid ();
#endif
}

perf::single::queue_stats_t ensure_queue_metrics_visible (
  const perf::single::queue_stats_t &input)
{
    perf::single::queue_stats_t out = input;
    if (!out.has_snd_pending) {
        out.has_snd_pending = true;
        out.snd_pending_max = 0.0;
    }
    if (!out.has_rcv_pending) {
        out.has_rcv_pending = true;
        out.rcv_pending_max = 0.0;
        out.rcv_pending_end = 0.0;
    }
    return out;
}

class gateway_queue_probe_t
{
  public:
    gateway_queue_probe_t (zlink::service::gateway_t *gateway_,
                           zlink::service::receiver_t *receiver_)
        : _gateway (gateway_),
          _receiver (receiver_),
          _sample_interval_ns (resolve_sample_interval_ns ()),
          _sample_every_msgs (resolve_sample_every_msgs ()),
          _send_last_sample_ns (0),
          _recv_last_sample_ns (0),
          _send_msgs_since_sample (0),
          _recv_msgs_since_sample (0),
          _snd_pending_max (0),
          _rcv_pending_max (0),
          _rcv_pending_end (0),
          _snd_seen (false),
          _rcv_seen (false)
    {
        resize_gateway_peers ();
        resize_receiver_peers ();
    }

    void sample_send_if_due () { maybe_sample_send (false); }
    void sample_recv_if_due () { maybe_sample_recv (false); }
    void force_sample_send () { maybe_sample_send (true); }
    void force_sample_recv () { maybe_sample_recv (true); }

    perf::single::queue_stats_t snapshot () const
    {
        perf::single::queue_stats_t out;
        if (_snd_seen) {
            out.has_snd_pending = true;
            out.snd_pending_max = static_cast<double> (_snd_pending_max);
        }
        if (_rcv_seen) {
            out.has_rcv_pending = true;
            out.rcv_pending_max = static_cast<double> (_rcv_pending_max);
            out.rcv_pending_end = static_cast<double> (_rcv_pending_end);
        }
        return ensure_queue_metrics_visible (out);
    }

  private:
    static unsigned long long resolve_sample_interval_ns ()
    {
        const int sample_ms = perf::single::resolve_single_queue_sample_ms ();
        const unsigned long long clamped_ms =
          static_cast<unsigned long long> (sample_ms > 0 ? sample_ms : 100);
        return clamped_ms * 1000000ULL;
    }

    static unsigned int resolve_sample_every_msgs ()
    {
        const int value = perf::single::resolve_single_queue_sample_every_msgs ();
        return static_cast<unsigned int> (value > 0 ? value : 64);
    }

    static unsigned long long now_ns ()
    {
        return static_cast<unsigned long long> (
          std::chrono::duration_cast<std::chrono::nanoseconds> (
            std::chrono::steady_clock::now ().time_since_epoch ())
            .count ());
    }

    static unsigned long long peer_activity_score (const zlink_peer_info_t &info)
    {
        return static_cast<unsigned long long> (info.msgs_sent)
               + static_cast<unsigned long long> (info.msgs_received);
    }

    static size_t choose_best_peer (const std::vector<zlink_peer_info_t> &peers,
                                    size_t count)
    {
        size_t best = 0;
        for (size_t i = 1; i < count; ++i) {
            const zlink_peer_info_t &cand = peers[i];
            const zlink_peer_info_t &cur = peers[best];
            if (cand.connected_time > cur.connected_time) {
                best = i;
                continue;
            }
            if (cand.connected_time == cur.connected_time
                && peer_activity_score (cand) > peer_activity_score (cur)) {
                best = i;
            }
        }
        return best;
    }

    void resize_gateway_peers ()
    {
        size_t count = 0;
        if (!_gateway || _gateway->router_peers (NULL, &count) != 0 || count == 0)
            return;
        _gateway_peers.resize (count);
    }

    void resize_receiver_peers ()
    {
        size_t count = 0;
        if (!_receiver || _receiver->router_peers (NULL, &count) != 0 || count == 0)
            return;
        _receiver_peers.resize (count);
    }

    bool read_gateway_peer (zlink_peer_info_t *info)
    {
        if (!_gateway || !info)
            return false;
        if (_gateway_peers.empty ())
            resize_gateway_peers ();
        if (_gateway_peers.empty ())
            return false;

        size_t count = _gateway_peers.size ();
        if (_gateway->router_peers (&_gateway_peers[0], &count) != 0 || count == 0)
            return false;
        *info = _gateway_peers[choose_best_peer (_gateway_peers, count)];
        return true;
    }

    bool read_receiver_peer (zlink_peer_info_t *info)
    {
        if (!_receiver || !info)
            return false;
        if (_receiver_peers.empty ())
            resize_receiver_peers ();
        if (_receiver_peers.empty ())
            return false;

        size_t count = _receiver_peers.size ();
        if (_receiver->router_peers (&_receiver_peers[0], &count) != 0
            || count == 0) {
            return false;
        }
        *info = _receiver_peers[choose_best_peer (_receiver_peers, count)];
        return true;
    }

    void maybe_sample_send (bool force)
    {
        if (!_gateway)
            return;

        if (force) {
            _send_msgs_since_sample = 0;
        } else if (_sample_every_msgs > 1) {
            ++_send_msgs_since_sample;
            if (_send_msgs_since_sample < _sample_every_msgs)
                return;
            _send_msgs_since_sample = 0;
        }

        const unsigned long long now = now_ns ();
        if (!force && _send_last_sample_ns > 0
            && now - _send_last_sample_ns < _sample_interval_ns) {
            return;
        }
        _send_last_sample_ns = now;

        zlink_peer_info_t info;
        if (!read_gateway_peer (&info))
            return;

        const unsigned long long pending =
          static_cast<unsigned long long> (info.snd_pending_msgs);
        if (!_snd_seen || pending > _snd_pending_max)
            _snd_pending_max = pending;
        _snd_seen = true;
    }

    void maybe_sample_recv (bool force)
    {
        if (!_receiver)
            return;

        if (force) {
            _recv_msgs_since_sample = 0;
        } else if (_sample_every_msgs > 1) {
            ++_recv_msgs_since_sample;
            if (_recv_msgs_since_sample < _sample_every_msgs)
                return;
            _recv_msgs_since_sample = 0;
        }

        const unsigned long long now = now_ns ();
        if (!force && _recv_last_sample_ns > 0
            && now - _recv_last_sample_ns < _sample_interval_ns) {
            return;
        }
        _recv_last_sample_ns = now;

        zlink_peer_info_t info;
        if (!read_receiver_peer (&info))
            return;

        const unsigned long long pending =
          static_cast<unsigned long long> (info.rcv_pending_msgs);
        if (!_rcv_seen || pending > _rcv_pending_max)
            _rcv_pending_max = pending;
        _rcv_pending_end = pending;
        _rcv_seen = true;
    }

    zlink::service::gateway_t *_gateway;
    zlink::service::receiver_t *_receiver;
    unsigned long long _sample_interval_ns;
    unsigned int _sample_every_msgs;
    unsigned long long _send_last_sample_ns;
    unsigned long long _recv_last_sample_ns;
    unsigned int _send_msgs_since_sample;
    unsigned int _recv_msgs_since_sample;
    unsigned long long _snd_pending_max;
    unsigned long long _rcv_pending_max;
    unsigned long long _rcv_pending_end;
    bool _snd_seen;
    bool _rcv_seen;
    std::vector<zlink_peer_info_t> _gateway_peers;
    std::vector<zlink_peer_info_t> _receiver_peers;
};

bool configure_receiver_tls (zlink::service::receiver_t &receiver,
                             const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    std::string cert;
    std::string key;
    std::string ca;
    if (!perf::single::try_resolve_perf_tls_paths (cert, key, ca))
        return false;

    return receiver.set_tls_server (cert, key) == 0;
}

bool configure_gateway_tls (zlink::service::gateway_t &gateway,
                            const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    std::string cert;
    std::string key;
    std::string ca;
    if (!perf::single::try_resolve_perf_tls_paths (cert, key, ca))
        return false;

    return gateway.set_tls_client (ca, "localhost", 0) == 0;
}

std::string bind_receiver_endpoint (zlink::service::receiver_t &receiver,
                                    const std::string &transport,
                                    int base_port)
{
    for (int i = 0; i < 64; ++i) {
        const std::string endpoint =
          perf::single::make_fixed_endpoint (transport, base_port + i);
        if (receiver.bind (endpoint) == 0)
            return endpoint;
    }
    return std::string ();
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
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    return false;
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
    return false;
}

int recv_provider_header (zlink::service::receiver_t &receiver,
                          std::vector<char> &recv_buffer,
                          zlink::recv_flag flags,
                          perf_single_metric::header_t *header_out,
                          bool *header_ok_out)
{
    if (header_ok_out)
        *header_ok_out = false;

    size_t received_size = 0;
    const int rc = receiver.recv (recv_buffer.data (),
                                  recv_buffer.size (),
                                  &received_size,
                                  flags);
    if (rc != 0) {
        const int err = errno;
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }
    if (received_size != recv_buffer.size ())
        return -1;

    bool header_ok = false;
    if (header_out) {
        header_ok = perf_single_metric::decode_payload_header (
          recv_buffer.data (), recv_buffer.size (), header_out);
    }

    if (header_ok_out)
        *header_ok_out = header_ok;
    return 1;
}

bool run_phase (zlink::service::gateway_t &gateway,
                const std::string &service,
                zlink::service::receiver_t &receiver,
                std::vector<char> &payload,
                size_t msg_size,
                uint32_t run_id,
                uint64_t &seq,
                perf_single_metric::phase_t phase,
                int warmup_count,
                int duration_s,
                int recv_timeout_ms,
                gateway_queue_probe_t *queue_probe,
                unsigned long long *received_out,
                perf::single::latency_stats_t *latency_out)
{
    if (!received_out)
        return false;

    const bool active = phase == perf_single_metric::phase_active;
    const size_t payload_size = payload.size ();
    const auto deadline =
      active ? std::chrono::steady_clock::now ()
                   + std::chrono::seconds (duration_s > 0 ? duration_s : 1)
             : std::chrono::steady_clock::time_point ();
    const auto drain_idle_limit =
      std::chrono::milliseconds (recv_timeout_ms > 0 ? recv_timeout_ms : 200);

    perf::single::latency_stats_builder_t latency_builder (
      perf::single::resolve_single_latency_sample_cap ());
    unsigned long long received = 0;
    std::atomic<bool> sender_done (false);
    std::atomic<bool> recv_failed (false);
    std::vector<char> recv_buffer (payload_size);

    auto account_header = [&] (const perf_single_metric::header_t &header,
                               bool header_ok) {
        if (active && queue_probe)
            queue_probe->sample_recv_if_due ();

        if (!header_ok
            || !perf_single_metric::is_expected (header, run_id, phase, msg_size)) {
            return;
        }

        ++received;
        if (!active)
            return;

        const uint64_t now = perf_single_metric::now_us ();
        const double latency_us =
          now >= header.sent_ts_us
            ? static_cast<double> (now - header.sent_ts_us)
            : 0.0;
        latency_builder.add (latency_us);
    };

    std::thread receiver_thread ([&] () {
        auto last_recv_at = std::chrono::steady_clock::now ();

        if (active && queue_probe)
            queue_probe->force_sample_recv ();

        while (true) {
            const bool done = sender_done.load (std::memory_order_acquire);
            const zlink::recv_flag flags =
              done ? zlink::recv_flag::dontwait : zlink::recv_flag::none;

            perf_single_metric::header_t header;
            bool header_ok = false;
            int recv_rc =
              recv_provider_header (receiver, recv_buffer, flags, &header, &header_ok);
            if (recv_rc > 0) {
                last_recv_at = std::chrono::steady_clock::now ();
                account_header (header, header_ok);

                for (;;) {
                    perf_single_metric::header_t burst_header;
                    bool burst_header_ok = false;
                    recv_rc = recv_provider_header (receiver,
                                                    recv_buffer,
                                                    zlink::recv_flag::dontwait,
                                                    &burst_header,
                                                    &burst_header_ok);
                    if (recv_rc > 0) {
                        last_recv_at = std::chrono::steady_clock::now ();
                        account_header (burst_header, burst_header_ok);
                        continue;
                    }

                    const int err = errno;
                    if (recv_rc == 0 || err == EAGAIN)
                        break;
                    if (err == EINTR)
                        continue;

                    recv_failed.store (true, std::memory_order_release);
                    break;
                }

                if (recv_failed.load (std::memory_order_acquire))
                    break;
                continue;
            }

            const int err = errno;
            if (err == EINTR)
                continue;
            if (err == EAGAIN) {
                if (done
                    && std::chrono::steady_clock::now () - last_recv_at
                         >= drain_idle_limit) {
                    break;
                }
                std::this_thread::yield ();
                continue;
            }

            recv_failed.store (true, std::memory_order_release);
            break;
        }

        if (active && queue_probe)
            queue_probe->force_sample_recv ();
    });

    bool send_failed = false;
    if (active && queue_probe)
        queue_probe->force_sample_send ();

    if (active) {
        while (std::chrono::steady_clock::now () < deadline) {
            const uint64_t sent_ts = perf_single_metric::now_us ();
            if (!perf_single_metric::stamp_payload (payload.data (),
                                                    payload_size,
                                                    run_id,
                                                    phase,
                                                    msg_size,
                                                    seq++,
                                                    sent_ts)
                || gateway.send (service,
                                 payload.data (),
                                 payload_size,
                                 zlink::send_flag::none)
                     != 0) {
                send_failed = true;
                break;
            }
            if (queue_probe)
                queue_probe->sample_send_if_due ();
        }
    } else {
        for (int i = 0; i < warmup_count; ++i) {
            if (!perf_single_metric::stamp_payload (payload.data (),
                                                    payload_size,
                                                    run_id,
                                                    phase,
                                                    msg_size,
                                                    seq++,
                                                    perf_single_metric::now_us ())
                || gateway.send (service,
                                 payload.data (),
                                 payload_size,
                                 zlink::send_flag::none)
                     != 0) {
                send_failed = true;
                break;
            }
        }
    }

    if (active && queue_probe)
        queue_probe->force_sample_send ();

    sender_done.store (true, std::memory_order_release);
    receiver_thread.join ();

    if (send_failed || recv_failed.load (std::memory_order_acquire))
        return false;

    if (queue_probe) {
        queue_probe->force_sample_send ();
        queue_probe->force_sample_recv ();
    }

    if (active) {
        if (received == 0 || latency_builder.count () == 0 || !latency_out)
            return false;
        *latency_out = latency_builder.snapshot ();
    } else if (received < static_cast<unsigned long long> (warmup_count)) {
        return false;
    }

    *received_out = received;
    return true;
}

} // namespace

void run_pattern_gateway (const std::string &transport,
                          size_t msg_size,
                          const std::string &lib_name)
{
    if (transport == "inproc" || transport == "ipc") {
        std::cout << "UNSUPPORTED,GATEWAY," << transport << std::endl;
        return;
    }
    if (!perf::single::transport_available (transport)) {
        std::cout << "UNSUPPORTED,GATEWAY," << transport << std::endl;
        return;
    }

    perf::single::ctx_guard_t ctx;
    if (!ctx.valid ()) {
        perf::single::print_fail_result (lib_name, "GATEWAY", transport, msg_size);
        return;
    }

    const std::string suffix =
      lib_name + "_gw_" + std::to_string (perf_single_metric::now_us ());
    const std::string reg_pub = "inproc://cpp_gw_pub_" + suffix;
    const std::string reg_router = "inproc://cpp_gw_router_" + suffix;
    const std::string service_name = "svc";

    zlink::service::registry_t registry (ctx.ctx ());
    if (!registry.valid () || registry.set_endpoints (reg_pub, reg_router) != 0
        || registry.start () != 0) {
        perf::single::print_fail_result (lib_name, "GATEWAY", transport, msg_size);
        return;
    }

    zlink::service::discovery_t discovery (ctx.ctx (),
                                           zlink::service_type::gateway);
    if (!discovery.valid () || discovery.connect_registry (reg_router) != 0) {
        perf::single::print_fail_result (lib_name, "GATEWAY", transport, msg_size);
        return;
    }

    zlink::service::gateway_t gateway (ctx.ctx (), discovery);
    zlink::service::receiver_t receiver (ctx.ctx ());
    if (!gateway.valid () || !receiver.valid ()) {
        perf::single::print_fail_result (lib_name, "GATEWAY", transport, msg_size);
        return;
    }

    const int sndhwm = perf::single::resolve_single_socket_hwm (true);
    const int rcvhwm = perf::single::resolve_single_socket_hwm (false);
    const int send_timeout = perf::single::resolve_single_send_timeout_ms ();
    const int recv_timeout = perf::single::resolve_single_recv_timeout_ms ();

    (void) gateway.set_sockopt (zlink::socket_options::sndhwm, sndhwm);
    (void) gateway.set_sockopt (zlink::socket_options::rcvhwm, rcvhwm);
    (void) gateway.set_sockopt (zlink::socket_options::sndtimeo, send_timeout);
    (void) gateway.set_sockopt (zlink::socket_options::rcvtimeo, recv_timeout);

    (void) receiver.set_sockopt (
      zlink::receiver_socket_role::router,
      zlink::socket_options::sndhwm,
      sndhwm);
    (void) receiver.set_sockopt (
      zlink::receiver_socket_role::router,
      zlink::socket_options::rcvhwm,
      rcvhwm);
    (void) receiver.set_sockopt (
      zlink::receiver_socket_role::router,
      zlink::socket_options::sndtimeo,
      send_timeout);
    (void) receiver.set_sockopt (
      zlink::receiver_socket_role::router,
      zlink::socket_options::rcvtimeo,
      recv_timeout);

    if (!configure_receiver_tls (receiver, transport)
        || !configure_gateway_tls (gateway, transport)) {
        perf::single::print_fail_result (lib_name, "GATEWAY", transport, msg_size);
        return;
    }

    const int base_port = 37000 + (bench_pid () % 1000) * 8;
    const std::string provider_endpoint =
      bind_receiver_endpoint (receiver, transport, base_port);
    if (provider_endpoint.empty ()
        || receiver.connect_registry (reg_router) != 0
        || receiver.register_service (service_name, provider_endpoint, 1) != 0) {
        perf::single::print_fail_result (lib_name, "GATEWAY", transport, msg_size);
        return;
    }

    if (!wait_discovery_ready (discovery, service_name, 1000)
        || !wait_gateway_ready (gateway, service_name, 1000)) {
        perf::single::print_fail_result (lib_name, "GATEWAY", transport, msg_size);
        return;
    }

    gateway_queue_probe_t queue_probe (&gateway, &receiver);
    auto print_fail_with_queue = [&] () {
        perf::single::print_queue_metrics (
          lib_name, "GATEWAY", transport, msg_size, queue_probe.snapshot ());
    };

    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 'a');

    const uint32_t run_id = static_cast<uint32_t> (perf_single_metric::now_us ());
    uint64_t seq = 1;
    perf::single::settle ();

    const int warmup_count =
      perf::single::resolve_bench_count ("PERF_WARMUP_COUNT", 200);
    unsigned long long warmup_received = 0;
    if (!run_phase (gateway,
                    service_name,
                    receiver,
                    payload,
                    msg_size,
                    run_id,
                    seq,
                    perf_single_metric::phase_warmup,
                    warmup_count,
                    0,
                    recv_timeout,
                    NULL,
                    &warmup_received,
                    NULL)) {
        print_fail_with_queue ();
        return;
    }

    const int duration_s = std::max (1, perf::single::resolve_single_duration_seconds ());
    unsigned long long received = 0;
    perf::single::latency_stats_t latency;
    if (!run_phase (gateway,
                    service_name,
                    receiver,
                    payload,
                    msg_size,
                    run_id,
                    seq,
                    perf_single_metric::phase_active,
                    0,
                    duration_s,
                    recv_timeout,
                    &queue_probe,
                    &received,
                    &latency)) {
        print_fail_with_queue ();
        return;
    }

    const double throughput =
      static_cast<double> (received) / static_cast<double> (duration_s);
    perf::single::print_result (lib_name,
                                "GATEWAY",
                                transport,
                                msg_size,
                                throughput,
                                latency.mean_us,
                                latency.p95_us,
                                latency.p99_us,
                                queue_probe.snapshot ());
}

int main (int argc, char **argv)
{
    return perf::single::run_standard_bench_main (argc, argv, run_pattern_gateway);
}
