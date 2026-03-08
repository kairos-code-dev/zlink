#include "../common/bench_common.hpp"
#include "../common/perf_single_metric_header.hpp"
#include <zlink.h>
#include <atomic>
#include <cstring>
#include <thread>

#if !defined(_WIN32)
#include <unistd.h>
#else
#include <process.h>
#endif

typedef int (*gateway_set_tls_client_fn)(void *, const char *, const char *, int);
typedef int (*provider_set_tls_server_fn)(void *, const char *, const char *);
typedef std::chrono::steady_clock steady_clock_t;

static bool wait_for_discovery(void *discovery,
                               void *monitor,
                               const char *service,
                               int timeout_ms);
static bool wait_for_gateway(void *gateway,
                             void *monitor,
                             const char *service,
                             int timeout_ms);

static const std::string &tls_ca_path()
{
    static std::string path =
      write_temp_cert(test_certs::ca_cert_pem, "gw_ca_cert");
    return path;
}

static const std::string &tls_cert_path()
{
    static std::string path =
      write_temp_cert(test_certs::server_cert_pem, "gw_server_cert");
    return path;
}

static const std::string &tls_key_path()
{
    static std::string path =
      write_temp_cert(test_certs::server_key_pem, "gw_server_key");
    return path;
}

static bool configure_gateway_tls(void *gateway,
                                  const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    gateway_set_tls_client_fn fn =
      reinterpret_cast<gateway_set_tls_client_fn>(
        resolve_symbol("zlink_gateway_set_tls_client"));
    if (!fn)
        return false;

    const std::string &ca = tls_ca_path();
    return fn(gateway, ca.c_str(), "localhost", 0) == 0;
}

static bool configure_provider_tls(void *provider,
                                   const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    provider_set_tls_server_fn fn =
      reinterpret_cast<provider_set_tls_server_fn>(
        resolve_symbol("zlink_receiver_set_tls_server"));
    if (!fn)
        return false;

    const std::string &cert = tls_cert_path();
    const std::string &key = tls_key_path();
    return fn(provider, cert.c_str(), key.c_str()) == 0;
}

static std::string bind_provider(void *provider,
                                 const std::string &transport,
                                 int base_port)
{
    for (int i = 0; i < 50; ++i) {
        const int port = base_port + i;
        std::string endpoint = make_fixed_endpoint(transport, port);
        if (zlink_receiver_bind(provider, endpoint.c_str()) == 0)
            return endpoint;
    }
    return std::string();
}

class recv_pending_probe_t {
public:
    recv_pending_probe_t(void *receiver_, const zlink_routing_id_t &routing_id_) :
        _receiver(receiver_),
        _routing_id(routing_id_),
        _sample_interval_ns(resolve_sample_interval_ns()),
        _last_sample_ns(0),
        _max_pending(0),
        _end_pending(0),
        _seen(false)
    {}

    void sample_if_due() { sample(false); }
    void force_sample() { sample(true); }
    void set_routing_id(const zlink_routing_id_t &routing_id_)
    {
        _routing_id = routing_id_;
    }
    bool seen() const { return _seen; }
    double max_pending() const { return static_cast<double>(_max_pending); }
    double end_pending() const { return static_cast<double>(_end_pending); }

private:
    static unsigned long long resolve_sample_interval_ns()
    {
        const int sample_ms = resolve_single_queue_sample_ms();
        const unsigned long long clamped_ms =
          static_cast<unsigned long long>(sample_ms > 0 ? sample_ms : 100);
        return clamped_ms * 1000000ULL;
    }

    static unsigned long long now_ns()
    {
        return static_cast<unsigned long long>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(
            steady_clock_t::now().time_since_epoch())
            .count());
    }

    void sample(bool force_)
    {
        if (!_receiver || _routing_id.size == 0)
            return;

        const unsigned long long now = now_ns();
        if (!force_ && _last_sample_ns > 0
            && now - _last_sample_ns < _sample_interval_ns) {
            return;
        }
        _last_sample_ns = now;

        zlink_peer_info_t info;
        memset(&info, 0, sizeof(info));
        if (zlink_receiver_peer_info(_receiver, &_routing_id, &info) != 0)
            return;

        const unsigned long long pending =
          static_cast<unsigned long long>(info.rcv_pending_msgs);
        if (!_seen || pending > _max_pending)
            _max_pending = pending;
        _end_pending = pending;
        _seen = true;
    }

    void *_receiver;
    zlink_routing_id_t _routing_id;
    unsigned long long _sample_interval_ns;
    unsigned long long _last_sample_ns;
    unsigned long long _max_pending;
    unsigned long long _end_pending;
    bool _seen;
};

static int recv_one_provider_message_flags (
  void *receiver,
  size_t payload_size,
  int flags,
  zlink_routing_id_t *out_rid,
  perf_single_metric::header_t *header_out,
  bool *header_ok_out)
{
    if (!receiver)
        return -1;

    if (header_ok_out)
        *header_ok_out = false;

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    zlink_routing_id_t rid;
    rid.size = 0;
    if (zlink_receiver_recv (receiver, &parts, &part_count, flags, &rid) < 0) {
        const int err = zlink_errno ();
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }
    if (part_count == 0 || !parts)
        return -1;
    if (out_rid)
        *out_rid = rid;

    zlink_msg_t &payload = parts[0];
    const size_t actual_size = zlink_msg_size (&payload);
    const bool size_ok = actual_size == payload_size;
    const bool has_more = part_count > 1;
    bool header_ok = false;

    if (size_ok && !has_more) {
        if (header_out) {
            header_ok = perf_single_metric::decode_payload_header (
              zlink_msg_data (&payload), actual_size, header_out);
        } else {
            header_ok = true;
        }
    }

    zlink_multipart_close (parts, part_count);

    if (!size_ok || has_more)
        return -1;

    if (header_ok_out)
        *header_ok_out = header_ok;

    return 1;
}

static bool send_one_gateway_metric (void *gateway,
                                     const char *service,
                                     std::vector<unsigned char> &payload,
                                     size_t payload_size,
                                     uint32_t run_id,
                                     perf_single_metric::phase_t phase,
                                     size_t msg_size,
                                     uint64_t seq,
                                     uint64_t sent_ts_us)
{
    if (!gateway || payload_size == 0)
        return false;
    if (payload.size () < payload_size)
        return false;

    if (!perf_single_metric::stamp_payload (payload.data (),
                                            payload_size,
                                            run_id,
                                            phase,
                                            msg_size,
                                            seq,
                                            sent_ts_us)) {
        return false;
    }

    return zlink_gateway_send_bytes (gateway,
                                     service,
                                     payload.data (),
                                     payload_size,
                                     0)
           == 0;
}

static bool run_gateway_phase (void *gateway,
                               const char *service_name,
                               void *provider,
                               size_t payload_size,
                               size_t msg_size,
                               uint32_t run_id,
                               uint64_t *seq,
                               perf_single_metric::phase_t phase,
                               int warmup_count,
                               int duration_s,
                               int recv_timeout_ms,
                               queue_probe_t *queue_probe,
                               recv_pending_probe_t *recv_probe,
                               unsigned long long *out_received,
                               latency_stats_t *out_stats)
{
    if (!gateway || !service_name || !provider || !seq || !out_received) {
        return false;
    }
    (void) recv_timeout_ms;

    const bool active_phase = phase == perf_single_metric::phase_active;
    const auto deadline =
      active_phase
        ? steady_clock_t::now ()
            + seconds_t (duration_s > 0 ? duration_s : 1)
        : steady_clock_t::time_point ();
    unsigned long long received = 0;
    latency_stats_builder_t latency_builder;
    std::vector<unsigned char> send_payload (payload_size);
    if (active_phase && queue_probe)
        queue_probe->force_sample_send ();
    if (active_phase && recv_probe)
        recv_probe->force_sample ();

    auto account_header =
      [&] (const zlink_routing_id_t &rid,
           const perf_single_metric::header_t &header,
           bool header_ok) {
          if (active_phase && recv_probe) {
              if (rid.size > 0)
                  recv_probe->set_routing_id (rid);
              recv_probe->sample_if_due ();
          }

          if (!header_ok
              || !perf_single_metric::is_expected (
                header, run_id, phase, msg_size)) {
              return;
          }

          ++received;
          if (!active_phase)
              return;

          const uint64_t now = perf_single_metric::now_us ();
          const double latency_us =
            now >= header.sent_ts_us
              ? static_cast<double> (now - header.sent_ts_us)
              : 0.0;
          latency_builder.add (latency_us);
      };

    unsigned long long iterations = 0;
    while (true) {
        if (active_phase) {
            if (steady_clock_t::now () >= deadline)
                break;
        } else if (iterations >= static_cast<unsigned long long> (warmup_count)) {
            break;
        }

        const uint64_t sent_ts = perf_single_metric::now_us ();
        if (!send_one_gateway_metric (gateway,
                                      service_name,
                                      send_payload,
                                      payload_size,
                                      run_id,
                                      phase,
                                      msg_size,
                                      (*seq)++,
                                      sent_ts)) {
            return false;
        }
        if (active_phase && queue_probe)
            queue_probe->sample_send_if_due ();

        zlink_routing_id_t rid;
        rid.size = 0;
        perf_single_metric::header_t header;
        bool header_ok = false;
        int recv_rc = recv_one_provider_message_flags (provider,
                                                       payload_size,
                                                       0,
                                                       &rid,
                                                       &header,
                                                       &header_ok);
        while (recv_rc == 0 && zlink_errno () == EINTR)
            recv_rc = recv_one_provider_message_flags (provider,
                                                       payload_size,
                                                       0,
                                                       &rid,
                                                       &header,
                                                       &header_ok);
        if (recv_rc <= 0)
            return false;
        account_header (rid, header, header_ok);

        for (;;) {
            zlink_routing_id_t burst_rid;
            burst_rid.size = 0;
            perf_single_metric::header_t burst_header;
            bool burst_header_ok = false;
            recv_rc = recv_one_provider_message_flags (provider,
                                                       payload_size,
                                                       ZLINK_DONTWAIT,
                                                       &burst_rid,
                                                       &burst_header,
                                                       &burst_header_ok);
            if (recv_rc > 0) {
                account_header (burst_rid, burst_header, burst_header_ok);
                continue;
            }
            if (recv_rc == 0 && zlink_errno () == EINTR)
                continue;
            if (recv_rc == 0)
                break;
            return false;
        }

        ++iterations;
    }

    if (active_phase && queue_probe)
        queue_probe->force_sample_send ();
    if (active_phase && recv_probe)
        recv_probe->force_sample ();

    *out_received = received;
    if (active_phase) {
        if (!out_stats || received == 0 || latency_builder.count () == 0)
            return false;
        *out_stats = latency_builder.snapshot ();
    } else if (received < iterations) {
        return false;
    }

    return true;
}

static queue_stats_t ensure_queue_metrics_visible(const queue_stats_t &input_)
{
    queue_stats_t out = input_;
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

void run_gateway(const std::string &transport,
                 size_t msg_size,
                 const std::string &lib_name)
{
    if (!transport_available(transport))
        return;

    if ((transport == "tls" || transport == "wss")
        && !resolve_symbol("zlink_gateway_set_tls_client")) {
        print_fail_result(lib_name, "GATEWAY", transport, msg_size);
        return;
    }

    ctx_guard_t ctx;
    if (!ctx.valid())
        return;

    std::string suffix = lib_name + "_gw_" + transport;
#if !defined(_WIN32)
    suffix += "_" + std::to_string(getpid());
#else
    suffix += "_" + std::to_string(_getpid());
#endif

    std::string reg_pub = "inproc://gw_pub_" + suffix;
    std::string reg_router = "inproc://gw_router_" + suffix;
    const char *service_name = "svc";

    void *registry = NULL;
    void *discovery = NULL;
    void *gateway = NULL;
    void *provider = NULL;
    void *discovery_monitor = NULL;
    void *gateway_monitor = NULL;
    recv_pending_probe_t *recv_probe = NULL;

    auto cleanup = [&]() {
        if (recv_probe) {
            delete recv_probe;
            recv_probe = NULL;
        }
        if (gateway_monitor)
            zlink_service_monitor_close(&gateway_monitor);
        if (discovery_monitor)
            zlink_service_monitor_close(&discovery_monitor);
        if (provider)
            zlink_receiver_destroy(&provider);
        if (gateway)
            zlink_gateway_destroy(&gateway);
        if (discovery)
            zlink_discovery_destroy(&discovery);
        if (registry)
            zlink_registry_destroy(&registry);
    };

    auto fail_no_queue = [&]() {
        print_fail_result(lib_name, "GATEWAY", transport, msg_size);
        cleanup();
    };

    registry = zlink_registry_new(ctx.get());
    if (!registry)
        return;

    if (zlink_registry_set_endpoints(registry, reg_pub.c_str(),
                                     reg_router.c_str())
        != 0
        || zlink_registry_start(registry) != 0) {
        cleanup();
        return;
    }

    discovery = zlink_discovery_new_typed(ctx.get(), ZLINK_SERVICE_TYPE_GATEWAY);
    if (!discovery) {
        cleanup();
        return;
    }
    discovery_monitor = zlink_discovery_monitor_open(
      discovery, ZLINK_DISCOVERY_SERVICE_UP);
    if (!discovery_monitor) {
        cleanup();
        return;
    }
    zlink_discovery_connect_registry(discovery, reg_pub.c_str());
    zlink_discovery_subscribe(discovery, service_name);

    gateway = zlink_gateway_new(ctx.get(), discovery, NULL);
    if (!gateway) {
        cleanup();
        return;
    }
    gateway_monitor =
      zlink_gateway_monitor_open(gateway, ZLINK_GATEWAY_SERVICE_READY);
    if (!gateway_monitor) {
        cleanup();
        return;
    }

    provider = zlink_receiver_new(ctx.get(), NULL);
    if (!provider) {
        cleanup();
        return;
    }

    const int gateway_sndhwm = resolve_single_socket_hwm(true);
    const int receiver_rcvhwm = resolve_single_socket_hwm(false);
    const int gateway_rcvhwm = resolve_single_socket_hwm(false);
    const int receiver_sndhwm = resolve_single_socket_hwm(true);
    const int send_timeout_ms = resolve_single_send_timeout_ms();
    const int recv_timeout_ms = resolve_single_recv_timeout_ms();

    // Apply benchmark options by service role before bind/connect:
    // gateway(sender): SNDHWM + SNDTIMEO
    // receiver(router): RCVHWM + RCVTIMEO
    (void) zlink_gateway_set_option(gateway, ZLINK_GATEWAY_OPT_SNDHWM,
                                    &gateway_sndhwm, sizeof(gateway_sndhwm));
    (void) zlink_gateway_set_option(gateway, ZLINK_GATEWAY_OPT_SNDTIMEO,
                                    &send_timeout_ms,
                                    sizeof(send_timeout_ms));
    (void) zlink_gateway_set_option(gateway, ZLINK_GATEWAY_OPT_RCVTIMEO,
                                    &recv_timeout_ms,
                                    sizeof(recv_timeout_ms));
    (void) zlink_gateway_set_option(gateway, ZLINK_GATEWAY_OPT_RCVHWM,
                                    &gateway_rcvhwm, sizeof(gateway_rcvhwm));
    (void) zlink_receiver_set_option(provider, ZLINK_RECEIVER_OPT_RCVHWM,
                                     &receiver_rcvhwm,
                                     sizeof(receiver_rcvhwm));
    (void) zlink_receiver_set_option(provider, ZLINK_RECEIVER_OPT_SNDHWM,
                                     &receiver_sndhwm,
                                     sizeof(receiver_sndhwm));
    (void) zlink_receiver_set_option(provider, ZLINK_RECEIVER_OPT_RCVTIMEO,
                                     &recv_timeout_ms,
                                     sizeof(recv_timeout_ms));
    (void) zlink_receiver_set_option(provider, ZLINK_RECEIVER_OPT_SNDTIMEO,
                                     &send_timeout_ms,
                                     sizeof(send_timeout_ms));

    if (!configure_provider_tls(provider, transport)) {
        fail_no_queue();
        return;
    }

    int base_port = 30000;
#if !defined(_WIN32)
    base_port += (getpid() % 2000);
#else
    base_port += (_getpid() % 2000);
#endif

    std::string provider_endpoint = bind_provider(provider, transport, base_port);
    if (provider_endpoint.empty()) {
        fail_no_queue();
        return;
    }

    if (zlink_receiver_connect_registry(provider, reg_router.c_str()) != 0
        || zlink_receiver_register(provider, service_name,
                                   provider_endpoint.c_str(), 1)
             != 0) {
        fail_no_queue();
        return;
    }

    queue_probe_t queue_probe(zlink_gateway_router_peers,
                              gateway,
                              NULL,
                              NULL);
    zlink_routing_id_t initial_rid;
    initial_rid.size = 0;
    recv_probe =
      new (std::nothrow) recv_pending_probe_t(provider, initial_rid);
    if (!recv_probe) {
        fail_no_queue();
        return;
    }

    auto snapshot_queue_stats = [&]() {
        queue_stats_t queue_stats =
          ensure_queue_metrics_visible(sample_queue_stats(&queue_probe));
        if (recv_probe && recv_probe->seen()) {
            queue_stats.has_rcv_pending = true;
            queue_stats.rcv_pending_max = recv_probe->max_pending();
            queue_stats.rcv_pending_end = recv_probe->end_pending();
        }
        return ensure_queue_metrics_visible(queue_stats);
    };

    auto fail = [&]() {
        const queue_stats_t queue_stats = snapshot_queue_stats();
        print_queue_metrics(lib_name, "GATEWAY", transport, msg_size, queue_stats);
        cleanup();
    };

    if (!configure_gateway_tls(gateway, transport)) {
        fail();
        return;
    }

    const size_t payload_size =
      std::max<size_t>(msg_size, perf_single_metric::header_size());
    if (!wait_for_discovery(discovery, discovery_monitor, service_name, 1000)
        || !wait_for_gateway(gateway, gateway_monitor, service_name, 1000)) {
        fail();
        return;
    }

    settle();
    const uint32_t run_id = static_cast<uint32_t>(perf_single_metric::now_us());
    uint64_t seq = 1;

    unsigned long long warmup_received = 0;
    const int warmup_count = resolve_bench_count("PERF_WARMUP_COUNT", 200);
    if (!run_gateway_phase(gateway,
                           service_name,
                           provider,
                           payload_size,
                           msg_size,
                           run_id,
                           &seq,
                           perf_single_metric::phase_warmup,
                           warmup_count,
                           0,
                           recv_timeout_ms,
                           NULL,
                           NULL,
                           &warmup_received,
                           NULL)) {
        fail();
        return;
    }

    const int duration_s = std::max(1, resolve_single_duration_seconds());
    unsigned long long received = 0;
    latency_stats_t latency_stats;
    if (!run_gateway_phase(gateway,
                           service_name,
                           provider,
                           payload_size,
                           msg_size,
                           run_id,
                           &seq,
                           perf_single_metric::phase_active,
                           0,
                           duration_s,
                           recv_timeout_ms,
                           &queue_probe,
                           recv_probe,
                           &received,
                           &latency_stats)) {
        fail();
        return;
    }

    const double throughput =
      static_cast<double>(received) / static_cast<double>(duration_s);
    const queue_stats_t queue_stats = snapshot_queue_stats();

    print_result(lib_name, "GATEWAY", transport, msg_size, throughput,
                 latency_stats.mean_us, latency_stats.p95_us,
                 latency_stats.p99_us, queue_stats);
    cleanup();
}

int main(int argc, char **argv)
{
    return run_standard_bench_main(argc, argv, run_gateway);
}
// Setup-only bounded wait. Measurement phases stay blocking send/recv based.
static bool wait_for_discovery(void *discovery,
                               void *monitor,
                               const char *service,
                               int timeout_ms)
{
    const auto deadline =
      steady_clock_t::now() + milliseconds_t(timeout_ms);
    zlink_service_event_t event;
    memset(&event, 0, sizeof(event));

    while (true) {
        if (zlink_discovery_service_available(discovery, service) > 0)
            return true;
        if (monitor
            && zlink_service_monitor_recv(monitor, &event, ZLINK_DONTWAIT) == 0
            && event.event_type == ZLINK_DISCOVERY_SERVICE_UP
            && std::strcmp(event.service_name, service) == 0) {
            return true;
        }
        if (steady_clock_t::now() >= deadline)
            return false;
        if (zlink_poll(NULL, 0, 1) < 0 && zlink_errno() != EINTR)
            return false;
    }
}

// Setup-only bounded wait. Measurement phases stay blocking send/recv based.
static bool wait_for_gateway(void *gateway,
                             void *monitor,
                             const char *service,
                             int timeout_ms)
{
    const auto deadline =
      steady_clock_t::now() + milliseconds_t(timeout_ms);
    zlink_service_event_t event;
    memset(&event, 0, sizeof(event));

    while (true) {
        if (zlink_gateway_connection_count(gateway, service) > 0)
            return true;
        if (monitor
            && zlink_service_monitor_recv(monitor, &event, ZLINK_DONTWAIT) == 0
            && event.event_type == ZLINK_GATEWAY_SERVICE_READY
            && std::strcmp(event.service_name, service) == 0) {
            return true;
        }
        if (steady_clock_t::now() >= deadline)
            return false;
        if (zlink_poll(NULL, 0, 1) < 0 && zlink_errno() != EINTR)
            return false;
    }
}
