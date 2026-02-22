#include "../common/bench_common.hpp"
#include "../common/bench_common_multi.hpp"
#include <zlink.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#else
#include <process.h>
#endif

typedef int (*spot_set_tls_server_fn)(void *, const char *, const char *);
typedef int (*spot_set_tls_client_fn)(void *, const char *, const char *, int);

static const std::string &tls_ca_path()
{
    static std::string path = write_temp_cert(test_certs::ca_cert_pem, "spot_ca_cert");
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

    const int rc = zlink_spot_pub_publish(spot_pub, topic.c_str(), &msg, 1, 0);
    if (rc != 0)
        zlink_msg_close(&msg);
    return rc == 0;
}

static bool spot_send_would_block()
{
    const int err = zlink_errno();
    return err == EAGAIN || err == EINTR;
}

static bool recv_spot_with_timeout(void *spot_sub, int timeout_ms)
{
    const int poll_sleep_us =
      resolve_bench_count("BENCH_SPOT_POLL_SLEEP_US", 0);
    const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (true) {
        zlink_msg_t *parts = NULL;
        size_t count = 0;
        char topic_out[256];
        size_t topic_len = 0;

        const int rc = zlink_spot_sub_recv(spot_sub, &parts, &count, ZLINK_DONTWAIT,
                                          topic_out, &topic_len);
        if (rc == 0) {
            if (parts)
                zlink_msgv_close(parts, count);
            return true;
        }

        const int err = zlink_errno();
        if (err != EAGAIN && err != EINTR)
            return false;
        if (std::chrono::steady_clock::now() >= deadline)
            return false;

        if (poll_sleep_us > 0)
            std::this_thread::sleep_for(std::chrono::microseconds(poll_sleep_us));
        else
            std::this_thread::yield();
    }
}

static bool recv_spot_any_with_timeout(const std::vector<void *> &spot_subs,
                                       int timeout_ms)
{
    if (spot_subs.empty())
        return false;

    const int poll_sleep_us =
      resolve_bench_count("BENCH_SPOT_POLL_SLEEP_US", 0);
    const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (true) {
        for (size_t i = 0; i < spot_subs.size(); ++i) {
            zlink_msg_t *parts = NULL;
            size_t count = 0;
            char topic_out[256];
            size_t topic_len = 0;
            const int rc =
              zlink_spot_sub_recv(spot_subs[i], &parts, &count, ZLINK_DONTWAIT,
                                  topic_out, &topic_len);
            if (rc == 0) {
                if (parts)
                    zlink_msgv_close(parts, count);
                return true;
            }

            const int err = zlink_errno();
            if (err != EAGAIN && err != EINTR)
                return false;
        }

        if (std::chrono::steady_clock::now() >= deadline)
            return false;

        if (poll_sleep_us > 0)
            std::this_thread::sleep_for(std::chrono::microseconds(poll_sleep_us));
        else
            std::this_thread::yield();
    }
}

void run_multi_spot(const std::string &transport,
                   size_t msg_size,
                   int /*msg_count*/,
                   const std::string &lib_name)
{
    if (!transport_available(transport))
        return;

    const std::vector<size_t> msg_sizes = resolve_bench_msg_sizes(msg_size);
    const auto emit_zero_from = [&](size_t start_index) {
        for (size_t i = start_index; i < msg_sizes.size(); ++i) {
            print_result(lib_name, "MULTI_SPOT", transport, msg_sizes[i], 0.0, 0.0);
        }
    };

    if ((transport == "tls" || transport == "wss")
        && !resolve_symbol("zlink_spot_node_set_tls_server")) {
        emit_zero_from(0);
        return;
    }

    const multi_bench_settings_t settings = resolve_multi_bench_settings();
    if (settings.clients == 0) {
        emit_zero_from(0);
        return;
    }
    const int sndtimeo_ms = resolve_bench_count("BENCH_MULTI_SNDTIMEO_MS", 5000);
    const int rcvtimeo_ms = resolve_bench_count("BENCH_MULTI_RCVTIMEO_MS", 5000);
    const int hwm = resolve_bench_count("BENCH_MULTI_HWM", 100000);

    void *ctx = zlink_ctx_new();
    if (!ctx) {
        emit_zero_from(0);
        return;
    }
    apply_ctx_options(ctx);

    void *node_pub = zlink_spot_node_new(ctx);
    if (!node_pub)
        return;

    std::vector<void *> node_subs(settings.clients, NULL);
    std::vector<void *> spot_subs;
    spot_subs.reserve(settings.clients);
    void *spot_pub = NULL;
    std::atomic<bool> receiver_stop(false);
    std::vector<std::thread> receiver_threads;

    auto stop_and_join_receivers = [&]() {
        receiver_stop.store(true, std::memory_order_release);
        for (size_t i = 0; i < receiver_threads.size(); ++i) {
            if (receiver_threads[i].joinable())
                receiver_threads[i].join();
        }
        receiver_threads.clear();
    };

    auto cleanup = [&]() {
        stop_and_join_receivers();
        if (spot_pub)
            zlink_spot_pub_destroy(&spot_pub);
        for (void *sub : spot_subs)
            if (sub)
                zlink_spot_sub_destroy(&sub);
        spot_subs.clear();
        for (void *node : node_subs)
            if (node)
                zlink_spot_node_destroy(&node);
        if (node_pub)
            zlink_spot_node_destroy(&node_pub);
    };

    auto fail = [&](const char *stage, double latency = 0.0) {
        if (bench_debug_enabled()) {
            std::fprintf(stderr,
                         "MULTI_SPOT fail(%s,size=%zu): %s\n",
                         transport.c_str(),
                         msg_sizes.empty() ? 0 : msg_sizes[0],
                         stage ? stage : "unknown");
        }
        emit_zero_from(0);
        cleanup();
    };

    if (!configure_spot_tls_server(node_pub, transport)) {
        fail("setup_server_tls");
        return;
    }
    {
        const int pub_mode = ZLINK_SPOT_NODE_PUB_MODE_ASYNC;
        const int pub_queue_hwm =
          resolve_bench_count("BENCH_MULTI_SPOT_PUB_QUEUE_HWM", 100000);
        const int full_policy = ZLINK_SPOT_NODE_PUB_QUEUE_FULL_EAGAIN;
        (void)zlink_spot_node_setsockopt(node_pub,
                                         ZLINK_SPOT_NODE_SOCKET_NODE,
                                         ZLINK_SPOT_NODE_OPT_PUB_MODE,
                                         &pub_mode,
                                         sizeof(pub_mode));
        (void)zlink_spot_node_setsockopt(node_pub,
                                         ZLINK_SPOT_NODE_SOCKET_NODE,
                                         ZLINK_SPOT_NODE_OPT_PUB_QUEUE_HWM,
                                         &pub_queue_hwm,
                                         sizeof(pub_queue_hwm));
        (void)zlink_spot_node_setsockopt(node_pub,
                                         ZLINK_SPOT_NODE_SOCKET_NODE,
                                         ZLINK_SPOT_NODE_OPT_PUB_QUEUE_FULL_POLICY,
                                         &full_policy,
                                         sizeof(full_policy));
    }

    for (size_t i = 0; i < settings.clients; ++i) {
        node_subs[i] = zlink_spot_node_new(ctx);
        if (!node_subs[i]) {
            fail("setup_node_sub");
            return;
        }
        if (!configure_spot_tls_client(node_subs[i], transport)) {
            fail("setup_client_tls");
            return;
        }
    }

    int base_port = 32000;
#if !defined(_WIN32)
    base_port += (getpid() % 2000);
#else
    base_port += (_getpid() % 2000);
#endif
    std::string endpoint = bind_spot_node(node_pub, transport, base_port);
    if (endpoint.empty()) {
        fail("bind");
        return;
    }

    for (size_t i = 0; i < node_subs.size(); ++i) {
        if (zlink_spot_node_connect_peer_pub(node_subs[i], endpoint.c_str()) != 0) {
            fail("connect_peer_pub");
            return;
        }
    }

    spot_pub = zlink_spot_pub_new(node_pub);
    if (!spot_pub) {
        fail("setup_pub");
        return;
    }
    {
        const int linger_ms = 0;
        zlink_spot_pub_setsockopt(spot_pub, ZLINK_LINGER, &linger_ms,
                                  sizeof(linger_ms));
        zlink_spot_pub_setsockopt(spot_pub, ZLINK_SNDTIMEO, &sndtimeo_ms,
                                  sizeof(sndtimeo_ms));
        zlink_spot_pub_setsockopt(spot_pub, ZLINK_SNDHWM, &hwm, sizeof(hwm));
    }

    for (size_t i = 0; i < node_subs.size(); ++i) {
        void *spot_sub = zlink_spot_sub_new(node_subs[i]);
        if (!spot_sub) {
            fail("setup_sub");
            return;
        }
        {
            const int linger_ms = 0;
            zlink_spot_sub_setsockopt(spot_sub, ZLINK_LINGER, &linger_ms,
                                      sizeof(linger_ms));
            zlink_spot_sub_setsockopt(spot_sub, ZLINK_RCVTIMEO, &rcvtimeo_ms,
                                      sizeof(rcvtimeo_ms));
            zlink_spot_sub_setsockopt(spot_sub, ZLINK_RCVHWM, &hwm, sizeof(hwm));
        }
        zlink_spot_sub_subscribe(spot_sub, "bench");
        spot_subs.push_back(spot_sub);
    }

    const std::string topic = "bench";
    settle();

    for (size_t s = 0; s < msg_sizes.size(); ++s) {
        const size_t current_size = msg_sizes[s];

        const bool secure_transport = transport == "tls" || transport == "wss";
        const int recv_timeout_ms =
          secure_transport
            ? std::max(rcvtimeo_ms, settings.connect_ready_timeout_ms * 8)
            : rcvtimeo_ms;
        const int warmup_count = resolve_bench_count("BENCH_WARMUP_COUNT", 200);
        bool round_failed = false;
        int warmup_done = 0;
        while (warmup_done < warmup_count) {
            if (!send_spot(spot_pub, topic, current_size)) {
                if (spot_send_would_block()) {
                    std::this_thread::yield();
                    continue;
                }
                round_failed = true;
                if (bench_debug_enabled()) {
                    std::fprintf(stderr,
                                 "MULTI_SPOT fail(%s,size=%zu): warmup\n",
                                 transport.c_str(),
                                 current_size);
                }
                break;
            }
            if (!recv_spot_any_with_timeout(spot_subs, recv_timeout_ms)) {
                round_failed = true;
                if (bench_debug_enabled()) {
                    std::fprintf(stderr,
                                 "MULTI_SPOT fail(%s,size=%zu): warmup_recv\n",
                                 transport.c_str(),
                                 current_size);
                }
                break;
            }
            ++warmup_done;
        }
        if (round_failed) {
            print_result(lib_name, "MULTI_SPOT", transport, current_size, 0.0, 0.0);
            emit_zero_from(s + 1);
            cleanup();
            return;
        }

        const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 200);
        stopwatch_t sw;
        sw.start();
        int latency_done = 0;
        while (latency_done < lat_count) {
            if (!send_spot(spot_pub, topic, current_size)) {
                if (spot_send_would_block()) {
                    std::this_thread::yield();
                    continue;
                }
                round_failed = true;
                if (bench_debug_enabled()) {
                    std::fprintf(stderr,
                                 "MULTI_SPOT fail(%s,size=%zu): latency\n",
                                 transport.c_str(),
                                 current_size);
                }
                break;
            }
            if (!recv_spot_any_with_timeout(spot_subs, recv_timeout_ms)) {
                round_failed = true;
                if (bench_debug_enabled()) {
                    std::fprintf(stderr,
                                 "MULTI_SPOT fail(%s,size=%zu): latency_recv\n",
                                 transport.c_str(),
                                 current_size);
                }
                break;
            }
            ++latency_done;
        }
        const double latency =
          (sw.elapsed_ms() * 1000.0) / std::max(1, lat_count);
        if (round_failed) {
            print_result(
              lib_name, "MULTI_SPOT", transport, current_size, 0.0, latency);
            emit_zero_from(s + 1);
            cleanup();
            return;
        }

        const auto measure_end =
          std::chrono::steady_clock::now()
          + std::chrono::seconds(std::max(1, settings.measure_seconds));
        const int measure_recv_timeout_ms =
          resolve_bench_count("BENCH_MULTI_SPOT_RECV_TIMEOUT_MS", 1);
        std::atomic<long> measure_received(0);
        std::atomic<long> total_received(0);
        receiver_stop.store(false, std::memory_order_release);
        receiver_threads.reserve(spot_subs.size());
        for (void *sub : spot_subs) {
            receiver_threads.push_back(std::thread([&, sub]() {
                while (!receiver_stop.load(std::memory_order_acquire)) {
                    const auto now = std::chrono::steady_clock::now();
                    if (now >= measure_end)
                        break;
                    if (!recv_spot_with_timeout(sub, measure_recv_timeout_ms))
                        continue;
                    total_received.fetch_add(1, std::memory_order_relaxed);
                    if (now < measure_end) {
                        measure_received.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }));
        }

        sw.start();
        long published = 0;
        while (std::chrono::steady_clock::now() < measure_end) {
            if (!send_spot(spot_pub, topic, current_size)) {
                if (spot_send_would_block()) {
                    std::this_thread::yield();
                    continue;
                }
                round_failed = true;
                if (bench_debug_enabled()) {
                    std::fprintf(stderr,
                                 "MULTI_SPOT fail(%s,size=%zu): measure_send\n",
                                 transport.c_str(),
                                 current_size);
                }
                break;
            }
            ++published;
        }

        stop_and_join_receivers();

        const long recv_measure = measure_received.load(std::memory_order_relaxed);
        if (round_failed || recv_measure <= 0) {
            if (!round_failed && bench_debug_enabled()) {
                std::fprintf(stderr,
                             "MULTI_SPOT fail(%s,size=%zu): measure\n",
                             transport.c_str(),
                             current_size);
            }
            print_result(
              lib_name, "MULTI_SPOT", transport, current_size, 0.0, latency);
            emit_zero_from(s + 1);
            cleanup();
            return;
        }

        const double throughput =
          static_cast<double>(recv_measure)
          / static_cast<double>(std::max(1, settings.measure_seconds));
        (void)total_received;
        print_result(lib_name,
                     "MULTI_SPOT",
                     transport,
                     current_size,
                     throughput,
                     latency);
        settle();
    }

    cleanup();
}

int main(int argc, char **argv)
{
    return run_standard_bench_main(argc, argv, run_multi_spot);
}
