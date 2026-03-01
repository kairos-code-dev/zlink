#include "../common/bench_common.hpp"
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
    while (std::chrono::steady_clock::now() < deadline) {
        const int count = zlink_discovery_receiver_count(discovery, service_name);
        if (count >= target)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
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
    while (std::chrono::steady_clock::now() < deadline) {
        size_t peer_count = 0;
        if (zlink_spot_node_sub_peers(node, NULL, &peer_count) == 0
            && peer_count > 0) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
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
    zlink_msg_t msg;
    if (zlink_msg_init_data(&msg,
                            const_cast<char *>(payload.data()),
                            send_size,
                            NULL,
                            NULL)
        != 0) {
        return false;
    }
    return zlink_spot_pub_publish(spot_pub, topic.c_str(), &msg, 1, 0) == 0;
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

static bool recv_spot_blocking(void *spot_sub)
{
    zlink_msg_t *parts = NULL;
    size_t count = 0;
    char topic_out[256];
    size_t topic_len = 0;
    const int rc =
      zlink_spot_sub_recv(spot_sub, &parts, &count, 0, topic_out, &topic_len);
    if (rc != 0)
        return false;
    if (parts)
        zlink_msgv_close(parts, count);
    return true;
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
        char topic_out[256];
        size_t topic_len = 0;
        const int rc = zlink_spot_sub_recv(spot_sub, &parts, &count,
                                           ZLINK_DONTWAIT,
                                           topic_out, &topic_len);
        if (rc == 0) {
            if (parts)
                zlink_msgv_close(parts, count);
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
            std::this_thread::yield();
        }
    }
}

static bool run_spot_warmup_and_latency(void *spot_pub,
                                        void *spot_sub,
                                        const std::string &topic,
                                        const std::vector<char> &payload,
                                        size_t msg_size,
                                        bool use_blocking_recv,
                                        int recv_timeout_ms,
                                        latency_stats_t *out_stats)
{
    if (!out_stats)
        return false;

    auto recv_one = [&]() {
        return use_blocking_recv ? recv_spot_blocking(spot_sub)
                                 : recv_spot_with_timeout_polling(spot_sub,
                                                                  recv_timeout_ms);
    };

    int warmup_count = resolve_bench_count("PERF_WARMUP_COUNT", 200);
    if (msg_size >= 65536 && warmup_count > 20)
        warmup_count = 20;
    for (int i = 0; i < warmup_count; ++i) {
        if (!send_spot(spot_pub, topic, payload, msg_size) || !recv_one())
            return false;
    }

    const int latency_duration_s = resolve_single_latency_duration_seconds();
    latency_stats_builder_t latency_builder;
    const auto latency_deadline =
      std::chrono::steady_clock::now()
      + std::chrono::seconds(latency_duration_s > 0 ? latency_duration_s : 1);
    while (std::chrono::steady_clock::now() < latency_deadline) {
        stopwatch_t per_message;
        per_message.start();
        if (!send_spot(spot_pub, topic, payload, msg_size) || !recv_one())
            return false;
        latency_builder.add(per_message.elapsed_ms() * 1000.0);
    }

    if (latency_builder.count() == 0)
        return false;

    *out_stats = latency_builder.snapshot();
    return true;
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

    while (std::chrono::steady_clock::now() < deadline) {
        const int send_rc = send_spot_try(spot_pub, topic, payload, 1);
        if (send_rc > 0
            && recv_spot_with_timeout_polling(spot_sub, per_try_timeout_ms)) {
            return true;
        }
        if (send_rc < 0)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return false;
}

static bool run_spot_throughput_parallel(void *spot_pub,
                                         void *spot_sub,
                                         bool use_blocking_recv,
                                         const std::string &topic,
                                         const std::vector<char> &payload,
                                         size_t msg_size,
                                         queue_probe_t *queue_probe,
                                         int recv_timeout_ms,
                                         int throughput_duration_s,
                                         int *out_received)
{
    if (!out_received)
        return false;

    std::atomic<bool> sender_done(false);
    std::atomic<bool> recv_failed(false);
    std::atomic<int> recv_count(0);
    const auto throughput_deadline =
      std::chrono::steady_clock::now()
      + std::chrono::seconds(throughput_duration_s > 0 ? throughput_duration_s
                                                       : 1);
    const auto drain_idle_limit =
      std::chrono::milliseconds(recv_timeout_ms > 0 ? recv_timeout_ms : 200);
    auto recv_one_flags = [&](int flags) {
        zlink_msg_t *parts = NULL;
        size_t count = 0;
        char topic_out[256];
        size_t topic_len = 0;
        const int rc = zlink_spot_sub_recv(
          spot_sub, &parts, &count, flags, topic_out, &topic_len);
        if (rc == 0) {
            if (parts)
                zlink_msgv_close(parts, count);
            return 1;
        }

        const int err = zlink_errno();
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    };

    std::thread receiver([&]() {
        auto last_recv_at = std::chrono::steady_clock::now();
        if (queue_probe)
            queue_probe->force_sample_recv();
        while (true) {
            const bool done = sender_done.load(std::memory_order_acquire);
            const int flags = use_blocking_recv && !done ? 0 : ZLINK_DONTWAIT;
            const int recv_rc = recv_one_flags(flags);
            if (recv_rc > 0) {
                last_recv_at = std::chrono::steady_clock::now();
                if (std::chrono::steady_clock::now() < throughput_deadline) {
                    recv_count.fetch_add(1, std::memory_order_release);
                }
                if (queue_probe)
                    queue_probe->sample_recv_if_due();

                // Drain immediately available messages without batch limits.
                for (;;) {
                    const int burst_rc = recv_one_flags(ZLINK_DONTWAIT);
                    if (burst_rc > 0) {
                        last_recv_at = std::chrono::steady_clock::now();
                        if (std::chrono::steady_clock::now()
                            < throughput_deadline) {
                            recv_count.fetch_add(1, std::memory_order_release);
                        }
                        if (queue_probe)
                            queue_probe->sample_recv_if_due();
                        continue;
                    }
                    if (burst_rc == 0)
                        break;
                    recv_failed.store(true, std::memory_order_release);
                    break;
                }
                if (recv_failed.load(std::memory_order_acquire))
                    break;
                continue;
            }

            if (recv_rc == 0) {
                if (done
                    && std::chrono::steady_clock::now() - last_recv_at
                         >= drain_idle_limit) {
                    break;
                }
                std::this_thread::yield();
                continue;
            }

            recv_failed.store(true, std::memory_order_release);
            break;
        }
        if (queue_probe)
            queue_probe->force_sample_recv();
    });

    bool send_failed = false;
    if (queue_probe)
        queue_probe->force_sample_send();
    while (std::chrono::steady_clock::now() < throughput_deadline) {
        const int send_rc = send_spot_try(spot_pub, topic, payload, msg_size);
        if (send_rc > 0) {
            if (queue_probe)
                queue_probe->sample_send_if_due();
            continue;
        }
        if (send_rc == 0) {
            std::this_thread::yield();
            continue;
        }
        if (send_rc < 0) {
            send_failed = true;
            break;
        }
    }
    if (queue_probe)
        queue_probe->force_sample_send();
    sender_done.store(true, std::memory_order_release);
    receiver.join();

    if (send_failed || recv_failed.load(std::memory_order_acquire))
        return false;

    const int received = recv_count.load(std::memory_order_acquire);
    *out_received = received > 0 ? received : 0;
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
        if (spot_pub)
            zlink_spot_pub_destroy(&spot_pub);
        if (spot_sub)
            zlink_spot_sub_destroy(&spot_sub);
        if (pub_registered && node_pub)
            (void) zlink_spot_node_unregister(node_pub, service_name);
        if (node_pub)
            zlink_spot_node_destroy(&node_pub);
        if (node_sub)
            zlink_spot_node_destroy(&node_sub);
        if (discovery)
            zlink_discovery_destroy(&discovery);
        if (registry)
            zlink_registry_destroy(&registry);
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
    bool use_blocking_recv = false;

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
      std::max<size_t>(std::max<size_t>(msg_size, static_cast<size_t>(64)),
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
        // Enforce benchmark socket options on active transport sockets.
        set_sockopt_int(pub_socket_unsafe, ZLINK_SNDHWM, sndhwm,
                        "ZLINK_SNDHWM");
        set_sockopt_int(pub_socket_unsafe, ZLINK_RCVHWM, rcvhwm,
                        "ZLINK_RCVHWM");
        set_sockopt_int(sub_socket_unsafe, ZLINK_SNDHWM, sndhwm,
                        "ZLINK_SNDHWM");
        set_sockopt_int(sub_socket_unsafe, ZLINK_RCVHWM, rcvhwm,
                        "ZLINK_RCVHWM");
        set_sockopt_int(pub_socket_unsafe, ZLINK_LINGER, linger_ms,
                        "ZLINK_LINGER");
        set_sockopt_int(sub_socket_unsafe, ZLINK_LINGER, linger_ms,
                        "ZLINK_LINGER");
        set_sockopt_int(pub_socket_unsafe, ZLINK_SNDTIMEO, send_timeout_ms,
                        "ZLINK_SNDTIMEO");
        set_sockopt_int(pub_socket_unsafe, ZLINK_RCVTIMEO, recv_timeout_ms,
                        "ZLINK_RCVTIMEO");
        set_sockopt_int(sub_socket_unsafe, ZLINK_SNDTIMEO, send_timeout_ms,
                        "ZLINK_SNDTIMEO");
        set_sockopt_int(pub_socket_unsafe, ZLINK_XPUB_NODROP, xpub_nodrop,
                        "ZLINK_XPUB_NODROP");
        set_sockopt_int(sub_socket_unsafe, ZLINK_RCVTIMEO, recv_timeout_ms,
                        "ZLINK_RCVTIMEO");
        queue_probe = new (std::nothrow) queue_probe_t(pub_socket_unsafe,
                                                       sub_socket_unsafe);
        if (!queue_probe) {
            fail();
            return;
        }
    }

    const int throughput_duration_s = resolve_single_duration_seconds();
    latency_stats_t latency_stats;
    if (!run_spot_warmup_and_latency(spot_pub,
                                     spot_sub,
                                     topic,
                                     payload,
                                     msg_size,
                                     use_blocking_recv, recv_timeout_ms,
                                     &latency_stats)) {
        fail();
        return;
    }

    int received = 0;
    if (!run_spot_throughput_parallel(spot_pub, spot_sub, use_blocking_recv,
                                      topic, payload, msg_size, queue_probe,
                                      recv_timeout_ms,
                                      throughput_duration_s,
                                      &received)) {
        fail();
        return;
    }

    const double throughput = static_cast<double>(received)
                              / static_cast<double>(throughput_duration_s);
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
