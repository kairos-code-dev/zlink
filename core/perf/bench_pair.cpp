#include "bench_common.hpp"
#include <zlink.h>
#include <thread>
#include <vector>
#include <cstring>

#ifndef ZLINK_TCP_NODELAY
#define ZLINK_TCP_NODELAY 26
#endif

void run_pair(const std::string& transport, size_t msg_size, int msg_count) {
    void *ctx = zlink_ctx_new();
    void *s_bind = zlink_socket(ctx, ZLINK_PAIR);
    void *s_conn = zlink_socket(ctx, ZLINK_PAIR);

    int nodelay = 1;
    zlink_setsockopt(s_bind, ZLINK_TCP_NODELAY, &nodelay, sizeof(nodelay));
    zlink_setsockopt(s_conn, ZLINK_TCP_NODELAY, &nodelay, sizeof(nodelay));

    int hwm = msg_count * 2;
    zlink_setsockopt(s_bind, ZLINK_SNDHWM, &hwm, sizeof(hwm));
    zlink_setsockopt(s_conn, ZLINK_RCVHWM, &hwm, sizeof(hwm));

    std::string endpoint = make_endpoint(transport, "zlink_pair");
    zlink_bind(s_bind, endpoint.c_str());
    zlink_connect(s_conn, endpoint.c_str());

    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(msg_size);
    stopwatch_t sw;

    // Warmup
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Latency
    int lat_count = 1000;
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        zlink_send(s_conn, buffer.data(), msg_size, 0);
        zlink_recv(s_bind, recv_buf.data(), msg_size, 0);
        zlink_send(s_bind, recv_buf.data(), msg_size, 0);
        zlink_recv(s_conn, recv_buf.data(), msg_size, 0);
    }
    double latency = (sw.elapsed_ms() * 1000.0) / (lat_count * 2);

    // Throughput
    std::thread receiver([&]() {
        for (int i = 0; i < msg_count; ++i) {
            zlink_recv(s_bind, recv_buf.data(), msg_size, 0);
        }
    });

    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        zlink_send(s_conn, buffer.data(), msg_size, 0);
    }
    receiver.join();
    double throughput = (double)msg_count / (sw.elapsed_ms() / 1000.0);

    print_result("libzlink", "PAIR", transport, msg_size, throughput, latency);

    zlink_close(s_bind);
    zlink_close(s_conn);
    zlink_ctx_term(ctx);
}

int main() {
    auto get_count = [](size_t size) {
        if (size <= 1024) return 100000;
        if (size <= 65536) return 20000;
        return 5000;
    };

    for (const auto& tr : TRANSPORTS) {
        for (size_t sz : MSG_SIZES) {
            run_pair(tr, sz, get_count(sz));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    return 0;
}
