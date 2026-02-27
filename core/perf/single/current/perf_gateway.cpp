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

    while (true) {
        if (zlink_discovery_service_available(discovery, service) > 0)
            return true;
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

static bool wait_for_gateway(void *gateway,
                             const char *service,
                             int timeout_ms)
{
    const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (true) {
        if (zlink_gateway_connection_count(gateway, service) > 0)
            return true;
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

static int recv_one_provider_message_flags(void *router, int flags)
{
    zlink_msg_t rid;
    zlink_msg_init(&rid);
    if (zlink_msg_recv(&rid, router, flags) < 0) {
        const int err = zlink_errno();
        zlink_msg_close(&rid);
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }
    if (!zlink_msg_more(&rid)) {
        zlink_msg_close(&rid);
        return -1;
    }

    zlink_msg_t payload;
    zlink_msg_init(&payload);
    if (zlink_msg_recv(&payload, router, 0) < 0) {
        zlink_msg_close(&rid);
        zlink_msg_close(&payload);
        return -1;
    }

    while (zlink_msg_more(&payload)) {
        zlink_msg_t part;
        zlink_msg_init(&part);
        if (zlink_msg_recv(&part, router, 0) < 0) {
            zlink_msg_close(&part);
            zlink_msg_close(&rid);
            zlink_msg_close(&payload);
            return -1;
        }
        zlink_msg_close(&part);
    }

    zlink_msg_close(&rid);
    zlink_msg_close(&payload);
    return 1;
}

static bool recv_one_provider_message(void *router)
{
    return recv_one_provider_message_flags(router, 0) > 0;
}

static bool send_one_gateway(void *gateway,
                             const char *service,
                             size_t msg_size)
{
    zlink_msg_t msg;
    zlink_msg_init_size(&msg, msg_size);
    if (msg_size > 0)
        memset(zlink_msg_data(&msg), 'a', msg_size);

    const int rc = zlink_gateway_send(gateway, service, &msg, 1, 0);
    if (rc != 0)
        zlink_msg_close(&msg);
    return rc == 0;
}

static bool run_gateway_warmup_and_latency(void *gateway,
                                           const char *service_name,
                                           void *provider_router,
                                           size_t msg_size,
                                           latency_stats_t *out_stats)
{
    if (!out_stats)
        return false;

    const int warmup_count = resolve_bench_count("PERF_WARMUP_COUNT", 200);
    for (int i = 0; i < warmup_count; ++i) {
        if (!send_one_gateway(gateway, service_name, msg_size)
            || !recv_one_provider_message(provider_router)) {
            return false;
        }
    }

    const int latency_duration_s = resolve_single_latency_duration_seconds();
    latency_stats_builder_t latency_builder;
    const auto latency_deadline =
      std::chrono::steady_clock::now()
      + std::chrono::seconds(latency_duration_s > 0 ? latency_duration_s : 1);
    while (std::chrono::steady_clock::now() < latency_deadline) {
        stopwatch_t per_message;
        per_message.start();
        if (!send_one_gateway(gateway, service_name, msg_size)
            || !recv_one_provider_message(provider_router)) {
            return false;
        }
        latency_builder.add(per_message.elapsed_ms() * 1000.0);
    }

    if (latency_builder.count() == 0)
        return false;

    *out_stats = latency_builder.snapshot();
    return true;
}

static bool run_gateway_throughput_parallel(void *gateway,
                                            const char *service_name,
                                            void *provider_router,
                                            size_t msg_size,
                                            queue_probe_t *queue_probe,
                                            int recv_timeout_ms,
                                            int throughput_duration_s,
                                            int *out_received)
{
    if (!out_received)
        return false;

    std::atomic<bool> sender_done(false);
    std::atomic<bool> send_failed(false);
    std::atomic<bool> recv_failed(false);
    std::atomic<int> received(0);
    const auto throughput_deadline =
      std::chrono::steady_clock::now()
      + std::chrono::seconds(throughput_duration_s > 0 ? throughput_duration_s
                                                       : 1);
    const auto drain_idle_limit =
      std::chrono::milliseconds(recv_timeout_ms > 0 ? recv_timeout_ms : 200);

    std::thread receiver([&]() {
        auto last_recv_at = std::chrono::steady_clock::now();
        if (queue_probe)
            queue_probe->force_sample_recv();
        while (true) {
            const bool done = sender_done.load(std::memory_order_acquire);
            const int flags = done ? ZLINK_DONTWAIT : 0;
            const int recv_rc =
              recv_one_provider_message_flags(provider_router, flags);
            if (recv_rc > 0) {
                last_recv_at = std::chrono::steady_clock::now();
                if (std::chrono::steady_clock::now() < throughput_deadline)
                    received.fetch_add(1, std::memory_order_release);
                if (queue_probe)
                    queue_probe->sample_recv_if_due();

                // Drain immediately available messages in a non-blocking burst.
                for (;;) {
                    const int burst_rc =
                      recv_one_provider_message_flags(provider_router,
                                                      ZLINK_DONTWAIT);
                    if (burst_rc > 0) {
                        last_recv_at = std::chrono::steady_clock::now();
                        if (std::chrono::steady_clock::now() < throughput_deadline)
                            received.fetch_add(1, std::memory_order_release);
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

    std::thread sender([&]() {
        if (queue_probe)
            queue_probe->force_sample_send();
        while (std::chrono::steady_clock::now() < throughput_deadline) {
            if (!send_one_gateway(gateway, service_name, msg_size)) {
                send_failed.store(true, std::memory_order_release);
                break;
            }
            if (queue_probe)
                queue_probe->sample_send_if_due();
        }
        if (queue_probe)
            queue_probe->force_sample_send();
        sender_done.store(true, std::memory_order_release);
    });

    sender.join();
    receiver.join();

    if (send_failed.load(std::memory_order_acquire)
        || recv_failed.load(std::memory_order_acquire)) {
        return false;
    }

    const int recv_count = received.load(std::memory_order_acquire);
    if (recv_count <= 0)
        return false;

    *out_received = recv_count;
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

    auto cleanup = [&]() {
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

    provider_router = zlink_receiver_router_socket_unsafe(provider);
    if (!provider_router) {
        fail_no_queue();
        return;
    }
    const int gateway_sndhwm = resolve_single_socket_hwm(true);
    const int receiver_rcvhwm = resolve_single_socket_hwm(false);
    const int send_timeout_ms = resolve_single_send_timeout_ms();
    const int recv_timeout_ms = resolve_single_recv_timeout_ms();

    // Apply benchmark options by service role:
    // gateway(sender): SNDHWM + SNDTIMEO
    // receiver(router): RCVHWM + RCVTIMEO
    (void) zlink_gateway_setsockopt(gateway, ZLINK_SNDHWM,
                                    &gateway_sndhwm,
                                    sizeof(gateway_sndhwm));
    (void) zlink_gateway_setsockopt(gateway, ZLINK_SNDTIMEO,
                                    &send_timeout_ms,
                                    sizeof(send_timeout_ms));
    if (zlink_receiver_setsockopt(provider,
                                  ZLINK_RECEIVER_SOCKET_ROUTER,
                                  ZLINK_RCVHWM,
                                  &receiver_rcvhwm,
                                  sizeof(receiver_rcvhwm))
        != 0) {
        set_sockopt_int(provider_router, ZLINK_RCVHWM, receiver_rcvhwm,
                        "ZLINK_RCVHWM");
    }
    if (zlink_receiver_setsockopt(provider,
                                  ZLINK_RECEIVER_SOCKET_ROUTER,
                                  ZLINK_RCVTIMEO,
                                  &recv_timeout_ms,
                                  sizeof(recv_timeout_ms))
        != 0) {
        set_sockopt_int(provider_router, ZLINK_RCVTIMEO, recv_timeout_ms,
                        "ZLINK_RCVTIMEO");
    }

    gateway_router = zlink_gateway_router_socket_unsafe(gateway);
    queue_probe_t queue_probe(gateway_router, provider_router);

    auto fail = [&]() {
        const queue_stats_t queue_stats =
          ensure_queue_metrics_visible(sample_queue_stats(&queue_probe));
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
    const int throughput_duration_s = resolve_single_duration_seconds();
    latency_stats_t latency_stats;
    if (!run_gateway_warmup_and_latency(gateway, service_name, provider_router,
                                        msg_size, &latency_stats)) {
        fail();
        return;
    }

    int recv_count = 0;
    if (!run_gateway_throughput_parallel(gateway, service_name, provider_router,
                                         msg_size, &queue_probe,
                                         recv_timeout_ms,
                                         throughput_duration_s, &recv_count)) {
        fail();
        return;
    }

    const double throughput = static_cast<double>(recv_count)
                              / static_cast<double>(throughput_duration_s);
    const queue_stats_t queue_stats =
      ensure_queue_metrics_visible(sample_queue_stats(&queue_probe));

    print_result(lib_name, "GATEWAY", transport, msg_size, throughput,
                 latency_stats.mean_us, latency_stats.p95_us,
                 latency_stats.p99_us, queue_stats);
    cleanup();
}

int main(int argc, char **argv)
{
    return run_standard_bench_main(argc, argv, run_gateway);
}
