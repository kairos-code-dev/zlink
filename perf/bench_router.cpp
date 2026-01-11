#include "bench_common.hpp"
#include <zmq.h>
#include <thread>
#include <vector>
#include <cstring>

#ifndef ZMQ_TCP_NODELAY
#define ZMQ_TCP_NODELAY 26
#endif

void run_router(const std::string& transport, size_t msg_size, int msg_count) {
    void *ctx = zmq_ctx_new();
    void *server = zmq_socket(ctx, ZMQ_ROUTER);
    void *client = zmq_socket(ctx, ZMQ_ROUTER);

    int nodelay = 1;
    zmq_setsockopt(server, ZMQ_TCP_NODELAY, &nodelay, sizeof(nodelay));
    zmq_setsockopt(client, ZMQ_TCP_NODELAY, &nodelay, sizeof(nodelay));

    int hwm = msg_count * 2;
    zmq_setsockopt(server, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(server, ZMQ_RCVHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(client, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(client, ZMQ_RCVHWM, &hwm, sizeof(hwm));

    zmq_setsockopt(client, ZMQ_ROUTING_ID, "CLIENT", 6);
    zmq_setsockopt(server, ZMQ_ROUTING_ID, "SERVER", 6);
    int handover = 1;
    zmq_setsockopt(server, ZMQ_ROUTER_HANDOVER, &handover, sizeof(handover));

    std::string endpoint = make_endpoint(transport, "zmq_router");
    zmq_bind(server, endpoint.c_str());
    zmq_connect(client, endpoint.c_str());

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::vector<char> payload(msg_size, 'x');
    std::vector<char> recv_buf(msg_size + 1024);
    stopwatch_t sw;

    // Latency
    int lat_count = 1000;
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        zmq_send(client, "SERVER", 6, ZMQ_SNDMORE);
        zmq_send(client, payload.data(), msg_size, 0);

        zmq_recv(server, recv_buf.data(), recv_buf.size(), 0);
        zmq_recv(server, recv_buf.data(), recv_buf.size(), 0);

        zmq_send(server, "CLIENT", 6, ZMQ_SNDMORE);
        zmq_send(server, payload.data(), msg_size, 0);

        zmq_recv(client, recv_buf.data(), recv_buf.size(), 0);
        zmq_recv(client, recv_buf.data(), recv_buf.size(), 0);
    }
    double latency = (sw.elapsed_ms() * 1000.0) / (lat_count * 2);

    // Throughput
    std::thread receiver([&]() {
        for (int i = 0; i < msg_count; ++i) {
            zmq_recv(server, recv_buf.data(), recv_buf.size(), 0);
            zmq_recv(server, recv_buf.data(), recv_buf.size(), 0);
        }
    });

    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        zmq_send(client, "SERVER", 6, ZMQ_SNDMORE);
        zmq_send(client, payload.data(), msg_size, 0);
    }
    receiver.join();
    double throughput = (double)msg_count / (sw.elapsed_ms() / 1000.0);

    print_result("libzmq", "ROUTER", transport, msg_size, throughput, latency);

    zmq_close(server);
    zmq_close(client);
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
            run_router(tr, sz, get_count(sz));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    return 0;
}
