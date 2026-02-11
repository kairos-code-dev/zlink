#include "../common/bench_common.hpp"
#include <zlink.h>
#include <thread>
#include <vector>
#include <string>
#include <atomic>
#include <cstdlib>
#include <cstring>

#if !defined(_WIN32)
#include <unistd.h>
#else
#include <process.h>
#endif

typedef int (*spot_set_tls_server_fn)(void *, const char *, const char *);
typedef int (*spot_set_tls_client_fn)(void *, const char *, const char *, int);

static const std::string &tls_ca_path() {
    static std::string path = write_temp_cert(test_certs::ca_cert_pem, "ca_cert");
    return path;
}

static const std::string &tls_cert_path() {
    static std::string path = write_temp_cert(test_certs::server_cert_pem, "server_cert");
    return path;
}

static const std::string &tls_key_path() {
    static std::string path = write_temp_cert(test_certs::server_key_pem, "server_key");
    return path;
}

static bool configure_spot_tls_server(void *node, const std::string &transport) {
    if (transport != "tls" && transport != "wss")
        return true;
    spot_set_tls_server_fn fn =
      reinterpret_cast<spot_set_tls_server_fn>(resolve_symbol("zlink_spot_node_set_tls_server"));
    if (!fn)
        return false;
    const std::string &cert = tls_cert_path();
    const std::string &key = tls_key_path();
    return fn(node, cert.c_str(), key.c_str()) == 0;
}

static bool configure_spot_tls_client(void *node, const std::string &transport) {
    if (transport != "tls" && transport != "wss")
        return true;
    spot_set_tls_client_fn fn =
      reinterpret_cast<spot_set_tls_client_fn>(resolve_symbol("zlink_spot_node_set_tls_client"));
    if (!fn)
        return false;
    const std::string &ca = tls_ca_path();
    const char *hostname = "localhost";
    const int trust_system = 0;
    return fn(node, ca.c_str(), hostname, trust_system) == 0;
}

static std::string bind_spot_node(void *node, const std::string &transport, int base_port) {
    for (int i = 0; i < 50; ++i) {
        const int port = base_port + i;
        std::string endpoint = make_fixed_endpoint(transport, port);
        if (zlink_spot_node_bind(node, endpoint.c_str()) == 0)
            return endpoint;
    }
    return std::string();
}

static bool send_spot(void *spot_pub, const std::string &topic, size_t msg_size) {
    zlink_msg_t msg;
    zlink_msg_init_size(&msg, msg_size);
    if (msg_size > 0)
        memset(zlink_msg_data(&msg), 'a', msg_size);
    return zlink_spot_pub_publish(spot_pub, topic.c_str(), &msg, 1, 0) == 0;
}

static bool recv_spot_with_timeout(void *spot_sub, int timeout_ms) {
    const int poll_sleep_us =
      resolve_bench_count("BENCH_SPOT_POLL_SLEEP_US", 0);
    const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        zlink_msg_t *parts = NULL;
        size_t count = 0;
        char topic_out[256];
        size_t topic_len = 0;
        const int rc =
          zlink_spot_sub_recv(spot_sub, &parts, &count, ZLINK_DONTWAIT, topic_out, &topic_len);
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

void run_spot(const std::string &transport, size_t msg_size, int msg_count,
              const std::string &lib_name) {
    if (!transport_available(transport))
        return;

    if ((transport == "tls" || transport == "wss")
        && !resolve_symbol("zlink_spot_node_set_tls_server")) {
        print_result(lib_name, "SPOT", transport, msg_size, 0.0, 0.0);
        return;
    }

    const int spot_msg_count_max =
      resolve_bench_count("BENCH_SPOT_MSG_COUNT_MAX", 50000);
    if (msg_count > spot_msg_count_max)
        msg_count = spot_msg_count_max;

    void *ctx = zlink_ctx_new();
    if (!ctx)
        return;

    void *node_pub = zlink_spot_node_new(ctx);
    void *node_sub = zlink_spot_node_new(ctx);
    if (!node_pub || !node_sub) {
        if (node_pub)
            zlink_spot_node_destroy(&node_pub);
        if (node_sub)
            zlink_spot_node_destroy(&node_sub);
        zlink_ctx_term(ctx);
        return;
    }

    if (!configure_spot_tls_server(node_pub, transport)
        || !configure_spot_tls_client(node_sub, transport)) {
        print_result(lib_name, "SPOT", transport, msg_size, 0.0, 0.0);
        zlink_spot_node_destroy(&node_pub);
        zlink_spot_node_destroy(&node_sub);
        zlink_ctx_term(ctx);
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
        print_result(lib_name, "SPOT", transport, msg_size, 0.0, 0.0);
        zlink_spot_node_destroy(&node_pub);
        zlink_spot_node_destroy(&node_sub);
        zlink_ctx_term(ctx);
        return;
    }

    if (zlink_spot_node_connect_peer_pub(node_sub, endpoint.c_str()) != 0) {
        print_result(lib_name, "SPOT", transport, msg_size, 0.0, 0.0);
        zlink_spot_node_destroy(&node_pub);
        zlink_spot_node_destroy(&node_sub);
        zlink_ctx_term(ctx);
        return;
    }

    void *spot_pub = zlink_spot_pub_new(node_pub);
    void *spot_sub = zlink_spot_sub_new(node_sub);
    if (!spot_pub || !spot_sub) {
        if (spot_pub)
            zlink_spot_pub_destroy(&spot_pub);
        if (spot_sub)
            zlink_spot_sub_destroy(&spot_sub);
        zlink_spot_node_destroy(&node_pub);
        zlink_spot_node_destroy(&node_sub);
        zlink_ctx_term(ctx);
        return;
    }

    const std::string topic = "bench";
    zlink_spot_sub_subscribe(spot_sub, topic.c_str());
    settle();

    const int recv_timeout_ms = 5000;
    const int warmup_count = resolve_bench_count("BENCH_WARMUP_COUNT", 200);
    bool ok = true;
    for (int i = 0; i < warmup_count; ++i) {
        if (!send_spot(spot_pub, topic, msg_size)
            || !recv_spot_with_timeout(spot_sub, recv_timeout_ms)) {
            ok = false;
            break;
        }
    }
    if (!ok) {
        print_result(lib_name, "SPOT", transport, msg_size, 0.0, 0.0);
        zlink_spot_pub_destroy(&spot_pub);
        zlink_spot_sub_destroy(&spot_sub);
        zlink_spot_node_destroy(&node_pub);
        zlink_spot_node_destroy(&node_sub);
        zlink_ctx_term(ctx);
        return;
    }

    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 200);
    stopwatch_t sw;
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        if (!send_spot(spot_pub, topic, msg_size)
            || !recv_spot_with_timeout(spot_sub, recv_timeout_ms)) {
            ok = false;
            break;
        }
    }
    if (!ok) {
        print_result(lib_name, "SPOT", transport, msg_size, 0.0, 0.0);
        zlink_spot_pub_destroy(&spot_pub);
        zlink_spot_sub_destroy(&spot_sub);
        zlink_spot_node_destroy(&node_pub);
        zlink_spot_node_destroy(&node_sub);
        zlink_ctx_term(ctx);
        return;
    }
    double latency = (sw.elapsed_ms() * 1000.0) / lat_count;

    std::atomic<int> recv_count(0);
    std::thread receiver([&]() {
        for (int i = 0; i < msg_count; ++i) {
            if (!recv_spot_with_timeout(spot_sub, recv_timeout_ms)) {
                break;
            }
            ++recv_count;
        }
    });

    int sent = 0;
    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        if (!send_spot(spot_pub, topic, msg_size)) {
            break;
        }
        ++sent;
    }
    receiver.join();
    const int received = recv_count.load();
    if (sent == 0 || received == 0) {
        print_result(lib_name, "SPOT", transport, msg_size, 0.0, latency);
        zlink_spot_pub_destroy(&spot_pub);
        zlink_spot_sub_destroy(&spot_sub);
        zlink_spot_node_destroy(&node_pub);
        zlink_spot_node_destroy(&node_sub);
        zlink_ctx_term(ctx);
        return;
    }
    const int effective = sent < received ? sent : received;
    const double elapsed_ms = sw.elapsed_ms();
    const double throughput =
      elapsed_ms > 0 ? (double)effective / (elapsed_ms / 1000.0) : 0.0;

    print_result(lib_name, "SPOT", transport, msg_size, throughput, latency);

    zlink_spot_pub_destroy(&spot_pub);
    zlink_spot_sub_destroy(&spot_sub);
    zlink_spot_node_destroy(&node_pub);
    zlink_spot_node_destroy(&node_sub);
    zlink_ctx_term(ctx);
}

int main(int argc, char **argv) {
    if (argc < 4)
        return 1;
    std::string lib_name = argv[1];
    std::string transport = argv[2];
    size_t size = std::stoul(argv[3]);
    int count = resolve_msg_count(size);
    run_spot(transport, size, count, lib_name);
    return 0;
}
