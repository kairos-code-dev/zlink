#include "bench_common.hpp"
#include <zlink.h>
#include <thread>
#include <vector>
#include <cstring>

#ifndef ZLINK_TCP_NODELAY
#define ZLINK_TCP_NODELAY 26
#endif

void run_pubsub(const std::string& transport, size_t msg_size, int msg_count) {
    void *ctx = zlink_ctx_new();
    void *pub = zlink_socket(ctx, ZLINK_XPUB);
    void *sub = zlink_socket(ctx, ZLINK_XSUB);

    int nodelay = 1;
    zlink_setsockopt(pub, ZLINK_TCP_NODELAY, &nodelay, sizeof(nodelay));
    zlink_setsockopt(sub, ZLINK_TCP_NODELAY, &nodelay, sizeof(nodelay));

    int hwm = msg_count * 2;
    zlink_setsockopt(pub, ZLINK_SNDHWM, &hwm, sizeof(hwm));
    zlink_setsockopt(pub, ZLINK_RCVHWM, &hwm, sizeof(hwm));
    zlink_setsockopt(sub, ZLINK_SNDHWM, &hwm, sizeof(hwm));
    zlink_setsockopt(sub, ZLINK_RCVHWM, &hwm, sizeof(hwm));

    std::string endpoint = make_endpoint(transport, "zlink_pubsub");
    zlink_bind(pub, endpoint.c_str());
    zlink_connect(sub, endpoint.c_str());

    // Subscribe
    char sub_cmd[] = {0x01};
    zlink_send(sub, sub_cmd, 1, 0);
    char buf[16];
    zlink_recv(pub, buf, sizeof(buf), 0);

    std::vector<char> payload(msg_size, 'p');
    std::vector<char> recv_buf(msg_size + 128);
    stopwatch_t sw;

    // Warmup
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Latency (One-way approx)
    int lat_count = 1000;
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        zlink_send(pub, payload.data(), msg_size, 0);
        zlink_recv(sub, recv_buf.data(), recv_buf.size(), 0);
    }
    double latency = (sw.elapsed_ms() * 1000.0) / lat_count;

    // Throughput
    std::thread receiver([&]() {
        for (int i = 0; i < msg_count; ++i) {
            zlink_recv(sub, recv_buf.data(), recv_buf.size(), 0);
        }
    });

    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        zlink_send(pub, payload.data(), msg_size, 0);
    }
    receiver.join();
    double throughput = (double)msg_count / (sw.elapsed_ms() / 1000.0);

    print_result("libzlink", "PUBSUB", transport, msg_size, throughput, latency);

    zlink_close(pub);
    zlink_close(sub);
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
            run_pubsub(tr, sz, get_count(sz));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    return 0;
}
