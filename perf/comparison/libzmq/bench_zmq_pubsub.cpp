#include "../common/bench_common.hpp"
#include <zmq.h>
#include <thread>
#include <vector>
#include <cstring>

#ifndef ZMQ_TCP_NODELAY
#define ZMQ_TCP_NODELAY 26
#endif

void run_pubsub(const std::string& transport, size_t msg_size, int msg_count) {
    void *ctx = zmq_ctx_new();
    void *pub = zmq_socket(ctx, ZMQ_XPUB);
    void *sub = zmq_socket(ctx, ZMQ_XSUB);

    int nodelay = 1;
    zmq_setsockopt(pub, ZMQ_TCP_NODELAY, &nodelay, sizeof(nodelay));
    zmq_setsockopt(sub, ZMQ_TCP_NODELAY, &nodelay, sizeof(nodelay));

    int hwm = msg_count * 2;
    zmq_setsockopt(pub, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(pub, ZMQ_RCVHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(sub, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(sub, ZMQ_RCVHWM, &hwm, sizeof(hwm));

    std::string endpoint = make_endpoint(transport, "zmq_pubsub");
    zmq_bind(pub, endpoint.c_str());
    zmq_connect(sub, endpoint.c_str());

    // Subscribe
    char sub_cmd[] = {0x01};
    zmq_send(sub, sub_cmd, 1, 0);
    char buf[16];
    zmq_recv(pub, buf, sizeof(buf), 0);

    std::vector<char> payload(msg_size, 'p');
    std::vector<char> recv_buf(msg_size + 128);
    stopwatch_t sw;

    // Latency (One-way approx)
    int lat_count = 1000;
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        zmq_send(pub, payload.data(), msg_size, 0);
        zmq_recv(sub, recv_buf.data(), recv_buf.size(), 0);
    }
    double latency = (sw.elapsed_ms() * 1000.0) / lat_count;

    // Throughput
    std::thread receiver([&]() {
        for (int i = 0; i < msg_count; ++i) {
            zmq_recv(sub, recv_buf.data(), recv_buf.size(), 0);
        }
    });

    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        zmq_send(pub, payload.data(), msg_size, 0);
    }
    receiver.join();
    double throughput = (double)msg_count / (sw.elapsed_ms() / 1000.0);

    print_result("libzmq", "PUBSUB", transport, msg_size, throughput, latency);

    zmq_close(pub);
    zmq_close(sub);
    zmq_ctx_term(ctx);
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
