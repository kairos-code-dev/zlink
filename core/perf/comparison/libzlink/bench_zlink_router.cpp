#include "../common/bench_common.hpp"
#include <zlink.h>
#include <thread>
#include <vector>
#include <cstring>

#ifndef ZLINK_TCP_NODELAY
#define ZLINK_TCP_NODELAY 26
#endif

void run_router(const std::string& transport, size_t msg_size, int msg_count) {
    void *ctx = zlink_ctx_new();
    void *server = zlink_socket(ctx, ZLINK_ROUTER);
    void *client = zlink_socket(ctx, ZLINK_ROUTER);

    int nodelay = 1;
    zlink_setsockopt(server, ZLINK_TCP_NODELAY, &nodelay, sizeof(nodelay));
    zlink_setsockopt(client, ZLINK_TCP_NODELAY, &nodelay, sizeof(nodelay));

    int hwm = msg_count * 2;
    zlink_setsockopt(server, ZLINK_SNDHWM, &hwm, sizeof(hwm));
    zlink_setsockopt(server, ZLINK_RCVHWM, &hwm, sizeof(hwm));
    zlink_setsockopt(client, ZLINK_SNDHWM, &hwm, sizeof(hwm));
    zlink_setsockopt(client, ZLINK_RCVHWM, &hwm, sizeof(hwm));
    
    zlink_setsockopt(client, ZLINK_IDENTITY, "CLIENT", 6);
    zlink_setsockopt(server, ZLINK_IDENTITY, "SERVER", 6);
    int handover = 1;
    zlink_setsockopt(server, ZLINK_ROUTER_HANDOVER, &handover, sizeof(handover));

    std::string endpoint = make_endpoint(transport, "zlink_router");
    zlink_bind(server, endpoint.c_str());
    zlink_connect(client, endpoint.c_str());

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::vector<char> payload(msg_size, 'x');
    std::vector<char> recv_buf(msg_size + 1024);
    stopwatch_t sw;

    // Latency
    int lat_count = 1000;
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        zlink_send(client, "SERVER", 6, ZLINK_SNDMORE);
        zlink_send(client, payload.data(), msg_size, 0);

        zlink_recv(server, recv_buf.data(), recv_buf.size(), 0);
        zlink_recv(server, recv_buf.data(), recv_buf.size(), 0);

        zlink_send(server, "CLIENT", 6, ZLINK_SNDMORE);
        zlink_send(server, payload.data(), msg_size, 0);

        zlink_recv(client, recv_buf.data(), recv_buf.size(), 0);
        zlink_recv(client, recv_buf.data(), recv_buf.size(), 0);
    }
    double latency = (sw.elapsed_ms() * 1000.0) / (lat_count * 2);

    // Throughput
    std::thread receiver([&]() {
        for (int i = 0; i < msg_count; ++i) {
            zlink_recv(server, recv_buf.data(), recv_buf.size(), 0);
            zlink_recv(server, recv_buf.data(), recv_buf.size(), 0);
        }
    });

    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        zlink_send(client, "SERVER", 6, ZLINK_SNDMORE);
        zlink_send(client, payload.data(), msg_size, 0);
    }
    receiver.join();
    double throughput = (double)msg_count / (sw.elapsed_ms() / 1000.0);

    print_result("libzlink", "ROUTER", transport, msg_size, throughput, latency);

    zlink_close(server);
    zlink_close(client);
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
            run_router(tr, sz, get_count(sz));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    return 0;
}
