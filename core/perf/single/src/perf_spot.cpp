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

typedef int (*spot_set_tls_server_fn)(void *, const char *, const char *);
typedef int (*spot_set_tls_client_fn)(void *, const char *, const char *, int);

static int current_process_id()
{
#if !defined(_WIN32)
    return static_cast<int>(getpid());
#else
    return static_cast<int>(_getpid());
#endif
}

static void configure_spot_idle_sleep_for_bench()
{
#if defined(_WIN32)
    _putenv_s("ZLINK_SPOT_IDLE_SLEEP_MS", "0");
#else
    setenv("ZLINK_SPOT_IDLE_SLEEP_MS", "0", 1);
#endif
}

static const std::string &tls_ca_path()
{
    static std::string path =
      write_temp_cert(test_certs::ca_cert_pem, "spot_ca_cert");
    return path;
}

static const std::string &tls_cert_path()
{
    static std::string path =
      write_temp_cert(test_certs::server_cert_pem, "spot_server_cert");
    return path;
}

static const std::string &tls_key_path()
{
    static std::string path =
      write_temp_cert(test_certs::server_key_pem, "spot_server_key");
    return path;
}

static bool configure_spot_tls_server(void *node,
                                      const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;
    spot_set_tls_server_fn fn =
      reinterpret_cast<spot_set_tls_server_fn>(
        resolve_symbol("zlink_spot_node_set_tls_server"));
    if (!fn)
        return false;
    const std::string &cert = tls_cert_path();
    const std::string &key = tls_key_path();
    return fn(node, cert.c_str(), key.c_str()) == 0;
}

static bool configure_spot_tls_client(void *node,
                                      const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;
    spot_set_tls_client_fn fn =
      reinterpret_cast<spot_set_tls_client_fn>(
        resolve_symbol("zlink_spot_node_set_tls_client"));
    if (!fn)
        return false;
    const std::string &ca = tls_ca_path();
    const char *hostname = "localhost";
    const int trust_system = 0;
    return fn(node, ca.c_str(), hostname, trust_system) == 0;
}

static std::string bind_spot_node(void *node,
                                  const std::string &transport,
                                  int base_port)
{
    for (int i = 0; i < 50; ++i) {
        const int port = base_port + i;
        std::string endpoint = make_fixed_endpoint(transport, port);
        if (zlink_spot_node_bind(node, endpoint.c_str()) == 0)
            return endpoint;
    }
    return std::string();
}

static bool setup_registry(void *ctx,
                           int base_port,
                           void **registry_out,
                           std::string *pub_endpoint_out,
                           std::string *router_endpoint_out)
{
    if (!ctx || !registry_out || !pub_endpoint_out || !router_endpoint_out)
        return false;

    *registry_out = NULL;
    pub_endpoint_out->clear();
    router_endpoint_out->clear();

    for (int attempt = 0; attempt < 32; ++attempt) {
        const int port_base = base_port + attempt * 3;
        const std::string pub_ep = make_fixed_endpoint("tcp", port_base + 1);
        const std::string router_ep = make_fixed_endpoint("tcp", port_base + 2);

        void *registry = zlink_registry_new(ctx);
        if (!registry)
            return false;

        if (zlink_registry_set_endpoints(registry, pub_ep.c_str(), router_ep.c_str())
              == 0
            && zlink_registry_start(registry) == 0) {
            *registry_out = registry;
            *pub_endpoint_out = pub_ep;
            *router_endpoint_out = router_ep;
            return true;
        }

        zlink_registry_destroy(&registry);
    }

    return false;
}

static bool wait_for_service_receivers(void *discovery,
                                       const char *service_name,
                                       int target_count,
                                       int timeout_ms)
{
    if (!discovery || !service_name || service_name[0] == '\0')
        return false;

    const int target = std::max(1, target_count);
    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(1000, timeout_ms));
    int sleep_ms = 1;
    while (std::chrono::steady_clock::now() < deadline) {
        const int count = zlink_discovery_receiver_count(discovery, service_name);
        if (count >= target)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        if (sleep_ms < 20)
            sleep_ms = std::min(20, sleep_ms * 2);
    }

    return zlink_discovery_receiver_count(discovery, service_name) >= target;
}

static bool wait_for_sub_peer_ready(void *node, int timeout_ms)
{
    if (!node)
        return false;

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(1000, timeout_ms));
    int sleep_ms = 1;
    while (std::chrono::steady_clock::now() < deadline) {
        size_t peer_count = 0;
        if (zlink_spot_node_sub_peers(node, NULL, &peer_count) == 0
            && peer_count > 0) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        if (sleep_ms < 20)
            sleep_ms = std::min(20, sleep_ms * 2);
    }

    size_t peer_count = 0;
    return zlink_spot_node_sub_peers(node, NULL, &peer_count) == 0
           && peer_count > 0;
}

static void apply_spot_node_options(void *node,
                                    int sndhwm,
                                    int rcvhwm,
                                    int send_timeout_ms,
                                    int recv_timeout_ms,
                                    int linger_ms,
                                    int xpub_nodrop)
{
    if (!node)
        return;

    (void) zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_PUB,
                                      ZLINK_SNDHWM, &sndhwm, sizeof(sndhwm));
    (void) zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_SUB,
                                      ZLINK_RCVHWM, &rcvhwm, sizeof(rcvhwm));
    (void) zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_DEALER,
                                      ZLINK_SNDHWM, &sndhwm, sizeof(sndhwm));
    (void) zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_DEALER,
                                      ZLINK_RCVHWM, &rcvhwm, sizeof(rcvhwm));

    (void) zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_PUB,
                                      ZLINK_LINGER, &linger_ms, sizeof(linger_ms));
    (void) zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_SUB,
                                      ZLINK_LINGER, &linger_ms, sizeof(linger_ms));
    (void) zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_DEALER,
                                      ZLINK_LINGER, &linger_ms, sizeof(linger_ms));

    (void) zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_PUB,
                                      ZLINK_SNDTIMEO, &send_timeout_ms,
                                      sizeof(send_timeout_ms));
    (void) zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_SUB,
                                      ZLINK_RCVTIMEO, &recv_timeout_ms,
                                      sizeof(recv_timeout_ms));
    (void) zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_DEALER,
                                      ZLINK_SNDTIMEO, &send_timeout_ms,
                                      sizeof(send_timeout_ms));
    (void) zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_DEALER,
                                      ZLINK_RCVTIMEO, &recv_timeout_ms,
                                      sizeof(recv_timeout_ms));

    (void) zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_PUB,
                                      ZLINK_XPUB_NODROP, &xpub_nodrop,
                                      sizeof(xpub_nodrop));
    (void) zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_SUB,
                                      ZLINK_XPUB_NODROP, &xpub_nodrop,
                                      sizeof(xpub_nodrop));
}

static bool send_spot(void *spot_pub,
                      const std::string &topic,
                      const std::vector<char> &payload,
                      size_t msg_size)
{
    if (!spot_pub || payload.empty())
        return false;

    const size_t send_size =
      std::min(payload.size(), std::max<size_t>(static_cast<size_t>(1), msg_size));
    const void *data =
      send_size > 0 ? static_cast<const void *>(payload.data()) : NULL;
    return zlink_spot_pub_publish_bytes(spot_pub,
                                        topic.c_str(),
                                        data,
                                        send_size,
                                        0)
           == 0;
}

// Return codes: 1=sent, 0=transient backpressure/interruption, -1=fatal.
static int send_spot_try(void *spot_pub,
                         const std::string &topic,
                         const std::vector<char> &payload,
                         size_t msg_size)
{
    if (send_spot(spot_pub, topic, payload, msg_size))
        return 1;
    const int err = zlink_errno();
    if (err == EAGAIN || err == EINTR)
        return 0;
    return -1;
}

static bool recv_spot_with_timeout_polling(void *spot_sub, int timeout_ms)
{
    const int poll_sleep_us =
      resolve_bench_count("PERF_SPOT_POLL_SLEEP_US", 0);
    const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (true) {
        zlink_msg_t *parts = NULL;
        size_t count = 0;
        const int rc = zlink_spot_sub_recv(spot_sub, &parts, &count,
                                           ZLINK_DONTWAIT,
                                           NULL, NULL);
        if (rc == 0) {
            if (parts)
                zlink_multipart_close(parts, count);
            return true;
        }

        if (zlink_errno() != EAGAIN)
            return false;
        if (std::chrono::steady_clock::now() >= deadline)
            return false;

        if (poll_sleep_us > 0) {
            std::this_thread::sleep_for(
              std::chrono::microseconds(poll_sleep_us));
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }
}

static int recv_spot_header_flags (void *spot_sub,
                                   size_t payload_size,
                                   int flags,
                                   perf_single_metric::header_t *header_out,
                                   bool *header_ok_out)
{
    if (!spot_sub)
        return -1;

    if (header_ok_out)
        *header_ok_out = false;

    zlink_msg_t *parts = NULL;
    size_t count = 0;
    const int rc = zlink_spot_sub_recv (
      spot_sub, &parts, &count, flags, NULL, NULL);
    if (rc != 0) {
        const int err = zlink_errno ();
        if (parts)
            zlink_multipart_close (parts, count);
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }

    if (count == 0 || !parts) {
        if (parts)
            zlink_multipart_close (parts, count);
        return -1;
    }

    const size_t actual_size = zlink_msg_size (&parts[0]);
    const bool size_ok = actual_size == payload_size;
    const bool single_part = count == 1;
    bool header_ok = false;
    if (size_ok && single_part) {
        if (header_out) {
            header_ok = perf_single_metric::decode_payload_header (
              zlink_msg_data (&parts[0]), actual_size, header_out);
        } else {
            header_ok = true;
        }
    }

    zlink_multipart_close (parts, count);

    if (!size_ok || !single_part)
        return -1;

    if (header_ok_out)
        *header_ok_out = header_ok;

    return 1;
}

static bool ensure_spot_subscription_ready(void *spot_pub,
                                           void *spot_sub,
                                           const std::string &topic,
                                           const std::vector<char> &payload,
                                           int recv_timeout_ms)
{
    const int ready_timeout_ms =
      resolve_bench_count("PERF_SPOT_READY_TIMEOUT_MS", 2000);
    const int per_try_timeout_ms =
      recv_timeout_ms > 0 ? std::min(recv_timeout_ms, 50) : 50;
    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(ready_timeout_ms > 0 ? ready_timeout_ms
                                                       : 2000);
    int sleep_ms = 1;

    while (std::chrono::steady_clock::now() < deadline) {
        const int send_rc = send_spot_try(spot_pub, topic, payload, 1);
        if (send_rc > 0
            && recv_spot_with_timeout_polling(spot_sub, per_try_timeout_ms)) {
            return true;
        }
        if (send_rc < 0)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        if (sleep_ms < 10)
            sleep_ms = std::min(10, sleep_ms * 2);
    }

    return false;
}

static bool run_spot_oneway_phase (void *spot_pub,
                                   void *spot_sub,
                                   const std::string &topic,
                                   std::vector<char> *payload,
                                   size_t payload_size,
                                   size_t msg_size,
                                   uint32_t run_id,
                                   uint64_t *seq,
                                   perf_single_metric::phase_t phase,
                                   int warmup_count,
                                   int duration_s,
                                   int recv_timeout_ms,
                                   queue_probe_t *queue_probe,
                                   unsigned long long *out_received,
                                   latency_stats_t *out_stats)
{
    if (!spot_pub || !spot_sub || !payload || !seq || !out_received)
        return false;

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
          [&] (const perf_single_metric::header_t &header, bool header_ok) {
              if (active_phase && queue_probe)
                  queue_probe->sample_recv_if_due ();

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

        if (active_phase && queue_probe)
            queue_probe->force_sample_recv ();

        while (true) {
            const bool done = sender_done.load (std::memory_order_acquire);
            const int flags = 0;

            perf_single_metric::header_t header;
            bool header_ok = false;
            const int recv_rc = recv_spot_header_flags (
              spot_sub, payload_size, flags, &header, &header_ok);
            if (recv_rc > 0) {
                last_recv_at = std::chrono::steady_clock::now ();
                account_header (header, header_ok);

                for (;;) {
                    perf_single_metric::header_t burst_header;
                    bool burst_header_ok = false;
                    const int burst_rc = recv_spot_header_flags (
                      spot_sub,
                      payload_size,
                      ZLINK_DONTWAIT,
                      &burst_header,
                      &burst_header_ok);
                    if (burst_rc > 0) {
                        last_recv_at = std::chrono::steady_clock::now ();
                        account_header (burst_header, burst_header_ok);
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

        if (active_phase && queue_probe)
            queue_probe->force_sample_recv ();
    });

    bool send_failed = false;
    if (active_phase && queue_probe)
        queue_probe->force_sample_send ();

    if (active_phase) {
        while (std::chrono::steady_clock::now () < deadline) {
            const uint64_t sent_ts = perf_single_metric::now_us ();
            if (!perf_single_metric::stamp_payload (payload->data (),
                                                    payload_size,
                                                    run_id,
                                                    phase,
                                                    msg_size,
                                                    (*seq)++,
                                                    sent_ts)) {
                send_failed = true;
                break;
            }

            const int send_rc =
              send_spot_try (spot_pub, topic, *payload, payload_size);
            if (send_rc <= 0) {
                send_failed = true;
                break;
            }

            if (queue_probe)
                queue_probe->sample_send_if_due ();
        }
    } else {
        for (int i = 0; i < warmup_count; ++i) {
            if (!perf_single_metric::stamp_payload (
                  payload->data (),
                  payload_size,
                  run_id,
                  phase,
                  msg_size,
                  (*seq)++,
                  perf_single_metric::now_us ())) {
                send_failed = true;
                break;
            }

            const int send_rc =
              send_spot_try (spot_pub, topic, *payload, payload_size);
            if (send_rc <= 0) {
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

void run_spot(const std::string &transport,
              size_t msg_size,
              const std::string &lib_name)
{
    configure_spot_idle_sleep_for_bench();

    if (!transport_available(transport))
        return;

    if ((transport == "tls" || transport == "wss")
        && !resolve_symbol("zlink_spot_node_set_tls_server")) {
        const queue_stats_t queue_stats =
          ensure_queue_metrics_visible(queue_stats_t());
        print_queue_metrics(lib_name, "SPOT", transport, msg_size, queue_stats);
        return;
    }

    ctx_guard_t ctx;
    if (!ctx.valid())
        return;
    {
        int blocky = 0;
        (void) zlink_ctx_set(ctx.get(), ZLINK_BLOCKY, blocky);
    }

    const char *service_name = "perf-spot";
    void *registry = NULL;
    void *discovery = NULL;
    void *node_pub = zlink_spot_node_new(ctx.get());
    void *node_sub = zlink_spot_node_new(ctx.get());
    void *spot_pub = NULL;
    void *spot_sub = NULL;
    queue_probe_t *queue_probe = NULL;
    bool pub_registered = false;

    auto cleanup = [&]() {
        if (queue_probe) {
            delete queue_probe;
            queue_probe = NULL;
        }
        if (spot_pub) {
            zlink_spot_pub_destroy(&spot_pub);
        }
        if (spot_sub) {
            zlink_spot_sub_destroy(&spot_sub);
        }
        if (pub_registered && node_pub) {
            (void) zlink_spot_node_unregister(node_pub, service_name);
        }
        if (node_pub) {
            zlink_spot_node_destroy(&node_pub);
        }
        if (node_sub) {
            zlink_spot_node_destroy(&node_sub);
        }
        if (discovery) {
            zlink_discovery_destroy(&discovery);
        }
        if (registry) {
            zlink_registry_destroy(&registry);
        }
    };

    auto fail_no_queue = [&]() {
        const queue_stats_t queue_stats =
          ensure_queue_metrics_visible(queue_stats_t());
        print_queue_metrics(lib_name, "SPOT", transport, msg_size, queue_stats);
        cleanup();
    };

    if (!node_pub || !node_sub) {
        fail_no_queue();
        return;
    }

    const int sndhwm = resolve_single_socket_hwm(true);
    const int rcvhwm = resolve_single_socket_hwm(false);
    const int send_timeout_ms = resolve_single_send_timeout_ms();
    const int recv_timeout_ms = resolve_single_recv_timeout_ms();
    const int linger_ms = 0;
    const int xpub_nodrop = 1;

    int base_port = 32000 + (current_process_id() % 2000) * 4;
    std::string registry_pub_endpoint;
    std::string registry_router_endpoint;
    if (!setup_registry(ctx.get(),
                        34000 + (current_process_id() % 20000),
                        &registry,
                        &registry_pub_endpoint,
                        &registry_router_endpoint)) {
        fail_no_queue();
        return;
    }

    discovery = zlink_discovery_new_typed(ctx.get(), ZLINK_SERVICE_TYPE_SPOT);
    if (!discovery
        || zlink_discovery_connect_registry(
             discovery, registry_pub_endpoint.c_str())
             != 0
        || zlink_discovery_subscribe(discovery, service_name) != 0) {
        fail_no_queue();
        return;
    }

    if (!configure_spot_tls_server(node_pub, transport)
        || !configure_spot_tls_server(node_sub, transport)
        || !configure_spot_tls_client(node_pub, transport)
        || !configure_spot_tls_client(node_sub, transport)) {
        fail_no_queue();
        return;
    }

    apply_spot_node_options(node_pub,
                            sndhwm,
                            rcvhwm,
                            send_timeout_ms,
                            recv_timeout_ms,
                            linger_ms,
                            xpub_nodrop);
    apply_spot_node_options(node_sub,
                            sndhwm,
                            rcvhwm,
                            send_timeout_ms,
                            recv_timeout_ms,
                            linger_ms,
                            xpub_nodrop);

    const std::string pub_endpoint = bind_spot_node(node_pub, transport, base_port);
    if (pub_endpoint.empty()) {
        fail_no_queue();
        return;
    }

    if (zlink_spot_node_connect_registry(node_pub, registry_router_endpoint.c_str())
        != 0) {
        fail_no_queue();
        return;
    }

    if (zlink_spot_node_register(node_pub, service_name, pub_endpoint.c_str()) != 0) {
        fail_no_queue();
        return;
    }
    pub_registered = true;

    if (zlink_spot_node_set_discovery(node_sub, discovery, service_name) != 0) {
        fail_no_queue();
        return;
    }

    const int discovery_timeout_ms =
      resolve_bench_count("PERF_SPOT_DISCOVERY_TIMEOUT_MS", 4000);
    if (!wait_for_service_receivers(discovery, service_name, 1, discovery_timeout_ms)) {
        fail_no_queue();
        return;
    }

    spot_pub = zlink_spot_pub_new(node_pub);
    spot_sub = zlink_spot_sub_new(node_sub);
    if (!spot_pub || !spot_sub) {
        fail_no_queue();
        return;
    }

    auto snapshot_queue_stats = [&]() {
        if (!queue_probe)
            return ensure_queue_metrics_visible(queue_stats_t());
        return ensure_queue_metrics_visible(sample_queue_stats(queue_probe));
    };

    auto fail = [&]() {
        const queue_stats_t queue_stats = snapshot_queue_stats();
        print_queue_metrics(lib_name, "SPOT", transport, msg_size, queue_stats);
        cleanup();
    };

    const std::string topic = "bench";
    zlink_spot_sub_subscribe(spot_sub, topic.c_str());
    if (!wait_for_sub_peer_ready(node_sub, discovery_timeout_ms)) {
        fail();
        return;
    }
    const size_t payload_capacity =
      std::max<size_t>(std::max<size_t>(
                         std::max<size_t>(msg_size,
                                          perf_single_metric::header_size()),
                         static_cast<size_t>(64)),
                       static_cast<size_t>(1));
    std::vector<char> payload(payload_capacity, 'a');
    settle();
    if (!ensure_spot_subscription_ready(spot_pub, spot_sub, topic, payload,
                                        recv_timeout_ms)) {
        fail();
        return;
    }

    void *pub_socket_unsafe = zlink_spot_node_pub_socket_unsafe(node_pub);
    void *sub_socket_unsafe = zlink_spot_node_sub_socket_unsafe(node_sub);
    if (pub_socket_unsafe && sub_socket_unsafe) {
        queue_probe = new (std::nothrow) queue_probe_t(pub_socket_unsafe,
                                                       sub_socket_unsafe);
        if (!queue_probe) {
            fail();
            return;
        }
    }

    const uint32_t run_id = static_cast<uint32_t>(perf_single_metric::now_us());
    uint64_t seq = 1;
    int warmup_count = resolve_bench_count("PERF_WARMUP_COUNT", 200);
    if (msg_size >= 65536 && warmup_count > 20)
        warmup_count = 20;

    unsigned long long warmup_received = 0;
    if (!run_spot_oneway_phase(spot_pub,
                               spot_sub,
                               topic,
                               &payload,
                               payload_capacity,
                               msg_size,
                               run_id,
                               &seq,
                               perf_single_metric::phase_warmup,
                               warmup_count,
                               0,
                               recv_timeout_ms,
                               NULL,
                               &warmup_received,
                               NULL)) {
        fail();
        return;
    }

    const int duration_s = std::max(1, resolve_single_duration_seconds());
    unsigned long long received = 0;
    latency_stats_t latency_stats;
    if (!run_spot_oneway_phase(spot_pub,
                               spot_sub,
                               topic,
                               &payload,
                               payload_capacity,
                               msg_size,
                               run_id,
                               &seq,
                               perf_single_metric::phase_active,
                               0,
                               duration_s,
                               recv_timeout_ms,
                               queue_probe,
                               &received,
                               &latency_stats)) {
        fail();
        return;
    }
    const double throughput =
      static_cast<double>(received) / static_cast<double>(duration_s);
    const queue_stats_t queue_stats = snapshot_queue_stats();

    print_result(lib_name, "SPOT", transport, msg_size, throughput,
                 latency_stats.mean_us, latency_stats.p95_us,
                 latency_stats.p99_us, queue_stats);
    cleanup();
}

int main(int argc, char **argv)
{
    return run_standard_bench_main(argc, argv, run_spot);
}
