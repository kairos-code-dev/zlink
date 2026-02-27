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

static bool send_spot(void *spot_pub,
                      const std::string &topic,
                      size_t msg_size)
{
    zlink_msg_t msg;
    zlink_msg_init_size(&msg, msg_size);
    if (msg_size > 0)
        memset(zlink_msg_data(&msg), 'a', msg_size);
    return zlink_spot_pub_publish(spot_pub, topic.c_str(), &msg, 1, 0) == 0;
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

    const int warmup_count = resolve_bench_count("PERF_WARMUP_COUNT", 200);
    for (int i = 0; i < warmup_count; ++i) {
        if (!send_spot(spot_pub, topic, msg_size) || !recv_one())
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
        if (!send_spot(spot_pub, topic, msg_size) || !recv_one())
            return false;
        latency_builder.add(per_message.elapsed_ms() * 1000.0);
    }

    if (latency_builder.count() == 0)
        return false;

    *out_stats = latency_builder.snapshot();
    return true;
}

static bool run_spot_throughput_parallel(void *spot_pub,
                                         void *spot_sub,
                                         bool use_blocking_recv,
                                         const std::string &topic,
                                         size_t msg_size,
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
        while (true) {
            const bool done = sender_done.load(std::memory_order_acquire);
            const int flags = use_blocking_recv && !done ? 0 : ZLINK_DONTWAIT;
            const int recv_rc = recv_one_flags(flags);
            if (recv_rc > 0) {
                if (std::chrono::steady_clock::now() < throughput_deadline) {
                    recv_count.fetch_add(1, std::memory_order_release);
                }

                // Drain immediately available messages without batch limits.
                for (;;) {
                    const int burst_rc = recv_one_flags(ZLINK_DONTWAIT);
                    if (burst_rc > 0) {
                        if (std::chrono::steady_clock::now()
                            < throughput_deadline) {
                            recv_count.fetch_add(1, std::memory_order_release);
                        }
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
                if (done)
                    break;
                std::this_thread::yield();
                continue;
            }

            recv_failed.store(true, std::memory_order_release);
            break;
        }
    });

    bool send_failed = false;
    while (std::chrono::steady_clock::now() < throughput_deadline) {
        if (!send_spot(spot_pub, topic, msg_size)) {
            send_failed = true;
            break;
        }
    }
    sender_done.store(true, std::memory_order_release);
    receiver.join();

    if (send_failed || recv_failed.load(std::memory_order_acquire))
        return false;

    const int received = recv_count.load(std::memory_order_acquire);
    if (received <= 0)
        return false;

    *out_received = received;
    return true;
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
        print_fail_result(lib_name, "SPOT", transport, msg_size);
        return;
    }

    ctx_guard_t ctx;
    if (!ctx.valid())
        return;

    void *node_pub = zlink_spot_node_new(ctx.get());
    void *node_sub = zlink_spot_node_new(ctx.get());
    void *spot_pub = NULL;
    void *spot_sub = NULL;

    auto cleanup = [&]() {
        if (spot_pub)
            zlink_spot_pub_destroy(&spot_pub);
        if (spot_sub)
            zlink_spot_sub_destroy(&spot_sub);
        if (node_pub)
            zlink_spot_node_destroy(&node_pub);
        if (node_sub)
            zlink_spot_node_destroy(&node_sub);
    };

    auto fail = [&]() {
        print_fail_result(lib_name, "SPOT", transport, msg_size);
        cleanup();
    };

    if (!node_pub || !node_sub) {
        cleanup();
        return;
    }

    if (!configure_spot_tls_server(node_pub, transport)
        || !configure_spot_tls_client(node_sub, transport)) {
        fail();
        return;
    }

    int base_port = 32000;
#if !defined(_WIN32)
    base_port += (getpid() % 2000);
#else
    base_port += (_getpid() % 2000);
#endif

    std::string endpoint = bind_spot_node(node_pub, transport, base_port);
    if (endpoint.empty()) {
        fail();
        return;
    }

    if (zlink_spot_node_connect_peer_pub(node_sub, endpoint.c_str()) != 0) {
        fail();
        return;
    }

    spot_pub = zlink_spot_pub_new(node_pub);
    spot_sub = zlink_spot_sub_new(node_sub);
    if (!spot_pub || !spot_sub) {
        cleanup();
        return;
    }

    const int recv_timeout_ms = 5000;
    bool use_blocking_recv = false;
    if (zlink_spot_sub_setsockopt(spot_sub, ZLINK_RCVTIMEO, &recv_timeout_ms,
                                  sizeof(recv_timeout_ms))
        == 0) {
        use_blocking_recv = true;
    }

    const std::string topic = "bench";
    zlink_spot_sub_subscribe(spot_sub, topic.c_str());
    settle();
    const int throughput_duration_s = resolve_single_duration_seconds();
    latency_stats_t latency_stats;
    if (!run_spot_warmup_and_latency(spot_pub, spot_sub, topic, msg_size,
                                     use_blocking_recv, recv_timeout_ms,
                                     &latency_stats)) {
        fail();
        return;
    }

    int received = 0;
    if (!run_spot_throughput_parallel(spot_pub, spot_sub, use_blocking_recv,
                                      topic, msg_size, throughput_duration_s,
                                      &received)) {
        fail();
        return;
    }

    const double throughput = static_cast<double>(received)
                              / static_cast<double>(throughput_duration_s);

    print_result(lib_name, "SPOT", transport, msg_size, throughput,
                 latency_stats.mean_us, latency_stats.p95_us,
                 latency_stats.p99_us);
    cleanup();
}

int main(int argc, char **argv)
{
    return run_standard_bench_main(argc, argv, run_spot);
}
