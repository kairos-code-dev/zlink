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

static bool wait_for_discovery(void *discovery,
                               const char *service,
                               int timeout_ms)
{
    const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    int sleep_ms = 1;

    while (true) {
        if (zlink_discovery_service_available(discovery, service) > 0)
            return true;
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        if (sleep_ms < 20)
            sleep_ms = std::min(20, sleep_ms * 2);
    }
}

static bool wait_for_gateway(void *gateway,
                             const char *service,
                             int timeout_ms)
{
    const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    int sleep_ms = 1;

    while (true) {
        if (zlink_gateway_connection_count(gateway, service) > 0)
            return true;
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        if (sleep_ms < 20)
            sleep_ms = std::min(20, sleep_ms * 2);
    }
}

class recv_pending_probe_t {
public:
    recv_pending_probe_t(void *socket_, const zlink_routing_id_t &routing_id_) :
        _socket(socket_),
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
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
    }

    void sample(bool force_)
    {
        if (!_socket || _routing_id.size == 0)
            return;

        const unsigned long long now = now_ns();
        if (!force_ && _last_sample_ns > 0
            && now - _last_sample_ns < _sample_interval_ns) {
            return;
        }
        _last_sample_ns = now;

        zlink_peer_info_t info;
        memset(&info, 0, sizeof(info));
        if (zlink_socket_peer_info(_socket, &_routing_id, &info) != 0)
            return;

        const unsigned long long pending =
          static_cast<unsigned long long>(info.rcv_pending_msgs);
        if (!_seen || pending > _max_pending)
            _max_pending = pending;
        _end_pending = pending;
        _seen = true;
    }

    void *_socket;
    zlink_routing_id_t _routing_id;
    unsigned long long _sample_interval_ns;
    unsigned long long _last_sample_ns;
    unsigned long long _max_pending;
    unsigned long long _end_pending;
    bool _seen;
};

static void copy_routing_id_from_msg(zlink_msg_t *rid_msg,
                                     zlink_routing_id_t *out_rid)
{
    if (!rid_msg || !out_rid)
        return;
    out_rid->size = 0;
    const size_t rid_size = zlink_msg_size(rid_msg);
    if (rid_size == 0)
        return;
    const size_t to_copy = std::min(rid_size, sizeof(out_rid->data));
    memcpy(out_rid->data, zlink_msg_data(rid_msg), to_copy);
    out_rid->size = static_cast<uint8_t>(to_copy);
}

static int recv_one_provider_message_flags (
  void *router,
  size_t payload_size,
  int flags,
  zlink_routing_id_t *out_rid,
  perf_single_metric::header_t *header_out,
  bool *header_ok_out)
{
    if (!router)
        return -1;

    if (header_ok_out)
        *header_ok_out = false;

    zlink_msg_t rid;
    zlink_msg_init (&rid);
    if (zlink_msg_recv (&rid, router, flags) < 0) {
        const int err = zlink_errno ();
        zlink_msg_close (&rid);
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }
    if (!zlink_msg_more (&rid)) {
        zlink_msg_close (&rid);
        return -1;
    }
    if (out_rid)
        copy_routing_id_from_msg (&rid, out_rid);
    zlink_msg_close (&rid);

    zlink_msg_t payload;
    zlink_msg_init (&payload);
    if (zlink_msg_recv (&payload, router, 0) < 0) {
        zlink_msg_close (&payload);
        return -1;
    }

    const size_t actual_size = zlink_msg_size (&payload);
    const bool size_ok = actual_size == payload_size;
    const bool has_more = zlink_msg_more (&payload) != 0;
    bool header_ok = false;

    if (size_ok && !has_more) {
        if (header_out) {
            header_ok = perf_single_metric::decode_payload_header (
              zlink_msg_data (&payload), actual_size, header_out);
        } else {
            header_ok = true;
        }
    }

    zlink_msg_close (&payload);

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

static bool run_gateway_oneway_phase (void *gateway,
                                      const char *service_name,
                                      void *provider_router,
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
    if (!gateway || !service_name || !provider_router || !seq || !out_received) {
        return false;
    }

    const bool active_phase = phase == perf_single_metric::phase_active;
    const auto deadline =
      active_phase
        ? std::chrono::steady_clock::now ()
            + std::chrono::seconds (duration_s > 0 ? duration_s : 1)
        : std::chrono::steady_clock::time_point ();
    const auto drain_idle_limit = std::chrono::milliseconds (
      recv_timeout_ms > 0 ? recv_timeout_ms : 200);

    std::atomic<bool> sender_done (false);
    std::atomic<bool> recv_failed (false);
    unsigned long long received = 0;
    latency_stats_builder_t latency_builder;

    std::thread receiver ([&] () {
        auto last_recv_at = std::chrono::steady_clock::now ();

        auto account_header =
          [&] (const zlink_routing_id_t &rid,
               const perf_single_metric::header_t &header,
               bool header_ok) {
              if (active_phase && recv_probe) {
                  if (rid.size > 0)
                      recv_probe->set_routing_id (rid);
                  recv_probe->sample_if_due ();
              }

              if (!header_ok || header.magic != perf_single_metric::k_magic
                  || header.phase != static_cast<uint32_t> (phase)) {
                  return;
              }

              if (active_phase) {
                  if (std::chrono::steady_clock::now () < deadline) {
                      ++received;
                      const uint64_t now = perf_single_metric::now_us ();
                      const double latency_us =
                        now >= header.sent_ts_us
                          ? static_cast<double> (now - header.sent_ts_us)
                          : 0.0;
                      latency_builder.add (latency_us);
                  }
              } else {
                  ++received;
              }
          };

        if (active_phase && recv_probe)
            recv_probe->force_sample ();

        while (true) {
            const bool done = sender_done.load (std::memory_order_acquire);
            const int flags = 0;

            zlink_routing_id_t rid;
            rid.size = 0;
            perf_single_metric::header_t header;
            bool header_ok = false;
            const int recv_rc = recv_one_provider_message_flags (provider_router,
                                                                 payload_size,
                                                                 flags,
                                                                 &rid,
                                                                 &header,
                                                                 &header_ok);
            if (recv_rc > 0) {
                last_recv_at = std::chrono::steady_clock::now ();
                account_header (rid, header, header_ok);

                for (;;) {
                    zlink_routing_id_t burst_rid;
                    burst_rid.size = 0;
                    perf_single_metric::header_t burst_header;
                    bool burst_header_ok = false;
                    const int burst_rc = recv_one_provider_message_flags (
                      provider_router,
                      payload_size,
                      ZLINK_DONTWAIT,
                      &burst_rid,
                      &burst_header,
                      &burst_header_ok);
                    if (burst_rc > 0) {
                        last_recv_at = std::chrono::steady_clock::now ();
                        account_header (
                          burst_rid, burst_header, burst_header_ok);
                        continue;
                    }
                    if (burst_rc == 0)
                        break;
                    recv_failed.store (true, std::memory_order_release);
                    break;
                }
                if (recv_failed.load (std::memory_order_acquire))
                    break;
                continue;
            }

            if (recv_rc == 0) {
                if (done
                    && std::chrono::steady_clock::now () - last_recv_at
                         >= drain_idle_limit) {
                    break;
                }
                continue;
            }

            recv_failed.store (true, std::memory_order_release);
            break;
        }

        if (active_phase && recv_probe)
            recv_probe->force_sample ();
    });

    bool send_failed = false;
    std::vector<unsigned char> send_payload (payload_size);
    if (active_phase && queue_probe)
        queue_probe->force_sample_send ();

    if (active_phase) {
        while (std::chrono::steady_clock::now () < deadline) {
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
                send_failed = true;
                break;
            }
            if (queue_probe)
                queue_probe->sample_send_if_due ();
        }
    } else {
        for (int i = 0; i < warmup_count; ++i) {
            if (!send_one_gateway_metric (gateway,
                                          service_name,
                                          send_payload,
                                          payload_size,
                                          run_id,
                                          phase,
                                          msg_size,
                                          (*seq)++,
                                          perf_single_metric::now_us ())) {
                send_failed = true;
                break;
            }
        }
    }

    if (active_phase && queue_probe)
        queue_probe->force_sample_send ();

    sender_done.store (true, std::memory_order_release);
    receiver.join ();

    if (send_failed || recv_failed.load (std::memory_order_acquire))
        return false;

    *out_received = received;

    if (active_phase) {
        if (received == 0 || latency_builder.count () == 0 || !out_stats)
            return false;
        *out_stats = latency_builder.snapshot ();
    } else if (received < static_cast<unsigned long long> (warmup_count)) {
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
    void *provider_router = NULL;
    void *gateway_router = NULL;
    recv_pending_probe_t *recv_probe = NULL;

    auto cleanup = [&]() {
        if (recv_probe) {
            delete recv_probe;
            recv_probe = NULL;
        }
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
    zlink_discovery_connect_registry(discovery, reg_pub.c_str());
    zlink_discovery_subscribe(discovery, service_name);

    gateway = zlink_gateway_new(ctx.get(), discovery, NULL);
    if (!gateway) {
        cleanup();
        return;
    }

    provider = zlink_receiver_new(ctx.get(), NULL);
    if (!provider) {
        cleanup();
        return;
    }

    provider_router = zlink_receiver_router_socket_unsafe(provider);
    if (!provider_router) {
        fail_no_queue();
        return;
    }
    gateway_router = zlink_gateway_router_socket_unsafe(gateway);
    if (!gateway_router) {
        fail_no_queue();
        return;
    }

    const int gateway_sndhwm = resolve_single_socket_hwm(true);
    const int receiver_rcvhwm = resolve_single_socket_hwm(false);
    const int gateway_rcvhwm = resolve_single_socket_hwm(false);
    const int receiver_sndhwm = resolve_single_socket_hwm(true);
    const int send_timeout_ms = resolve_single_send_timeout_ms();
    const int recv_timeout_ms = resolve_single_recv_timeout_ms();
    const int linger_ms = 0;

    // Apply benchmark options by service role before bind/connect:
    // gateway(sender): SNDHWM + SNDTIMEO
    // receiver(router): RCVHWM + RCVTIMEO
    (void) zlink_gateway_setsockopt(gateway, ZLINK_SNDHWM,
                                    &gateway_sndhwm,
                                    sizeof(gateway_sndhwm));
    (void) zlink_gateway_setsockopt(gateway, ZLINK_SNDTIMEO,
                                    &send_timeout_ms,
                                    sizeof(send_timeout_ms));
    (void) zlink_gateway_setsockopt(gateway, ZLINK_RCVTIMEO,
                                    &recv_timeout_ms,
                                    sizeof(recv_timeout_ms));
    (void) zlink_gateway_setsockopt(gateway, ZLINK_RCVHWM,
                                    &gateway_rcvhwm,
                                    sizeof(gateway_rcvhwm));
    (void) zlink_receiver_setsockopt(provider,
                                     ZLINK_RECEIVER_SOCKET_ROUTER,
                                     ZLINK_RCVHWM,
                                     &receiver_rcvhwm,
                                     sizeof(receiver_rcvhwm));
    (void) zlink_receiver_setsockopt(provider,
                                     ZLINK_RECEIVER_SOCKET_ROUTER,
                                     ZLINK_SNDHWM,
                                     &receiver_sndhwm,
                                     sizeof(receiver_sndhwm));
    (void) zlink_receiver_setsockopt(provider,
                                     ZLINK_RECEIVER_SOCKET_ROUTER,
                                     ZLINK_RCVTIMEO,
                                     &recv_timeout_ms,
                                     sizeof(recv_timeout_ms));
    (void) zlink_receiver_setsockopt(provider,
                                     ZLINK_RECEIVER_SOCKET_ROUTER,
                                     ZLINK_SNDTIMEO,
                                     &send_timeout_ms,
                                     sizeof(send_timeout_ms));

    // Enforce benchmark options on actual transport sockets.
    set_sockopt_int(gateway_router, ZLINK_SNDHWM, gateway_sndhwm,
                    "ZLINK_SNDHWM");
    set_sockopt_int(gateway_router, ZLINK_RCVHWM, gateway_rcvhwm,
                    "ZLINK_RCVHWM");
    set_sockopt_int(gateway_router, ZLINK_LINGER, linger_ms,
                    "ZLINK_LINGER");
    set_sockopt_int(gateway_router, ZLINK_SNDTIMEO, send_timeout_ms,
                    "ZLINK_SNDTIMEO");
    set_sockopt_int(gateway_router, ZLINK_RCVTIMEO, recv_timeout_ms,
                    "ZLINK_RCVTIMEO");
    set_sockopt_int(provider_router, ZLINK_SNDHWM, receiver_sndhwm,
                    "ZLINK_SNDHWM");
    set_sockopt_int(provider_router, ZLINK_RCVHWM, receiver_rcvhwm,
                    "ZLINK_RCVHWM");
    set_sockopt_int(provider_router, ZLINK_LINGER, linger_ms,
                    "ZLINK_LINGER");
    set_sockopt_int(provider_router, ZLINK_SNDTIMEO, send_timeout_ms,
                    "ZLINK_SNDTIMEO");
    set_sockopt_int(provider_router, ZLINK_RCVTIMEO, recv_timeout_ms,
                    "ZLINK_RCVTIMEO");

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

    queue_probe_t queue_probe(gateway_router, NULL);
    zlink_routing_id_t initial_rid;
    initial_rid.size = 0;
    recv_probe =
      new (std::nothrow) recv_pending_probe_t(provider_router, initial_rid);
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

    if (!wait_for_discovery(discovery, service_name, 1000)
        || !wait_for_gateway(gateway, service_name, 1000)) {
        fail();
        return;
    }

    settle();
    const size_t payload_size =
      std::max<size_t>(msg_size, perf_single_metric::header_size());
    const uint32_t run_id = static_cast<uint32_t>(perf_single_metric::now_us());
    uint64_t seq = 1;

    unsigned long long warmup_received = 0;
    const int warmup_count = resolve_bench_count("PERF_WARMUP_COUNT", 200);
    if (!run_gateway_oneway_phase(gateway,
                                  service_name,
                                  provider_router,
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
    if (!run_gateway_oneway_phase(gateway,
                                  service_name,
                                  provider_router,
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
