#include "../common/bench_common.hpp"
#include <zmq.h>
#include <thread>
#include <vector>
#include <cstring>

#ifndef ZMQ_TCP_NODELAY
#define ZMQ_TCP_NODELAY 26
#endif

void run_pair(const std::string& transport, size_t msg_size, int msg_count) {
    std::cerr << "run_pair: " << transport << " " << msg_size << "B" << std::endl;
    void *ctx = zmq_ctx_new();
    if (!ctx) { std::cerr << "ctx_new failed" << std::endl; return; }
    void *s_bind = zmq_socket(ctx, ZMQ_PAIR);
    if (!s_bind) { std::cerr << "socket bind failed" << std::endl; return; }
    void *s_conn = zmq_socket(ctx, ZMQ_PAIR);
    if (!s_conn) { std::cerr << "socket conn failed" << std::endl; return; }

    int nodelay = 1;
    zmq_setsockopt(s_bind, ZMQ_TCP_NODELAY, &nodelay, sizeof(nodelay));
    zmq_setsockopt(s_conn, ZMQ_TCP_NODELAY, &nodelay, sizeof(nodelay));

    int hwm = msg_count * 2;
    zmq_setsockopt(s_bind, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(s_conn, ZMQ_RCVHWM, &hwm, sizeof(hwm));

    std::string endpoint = make_endpoint(transport, "zmq_pair");
    std::cerr << "endpoint: " << endpoint << std::endl;
    int rc = zmq_bind(s_bind, endpoint.c_str());
    if (rc != 0) { std::cerr << "bind failed: " << zmq_strerror(errno) << std::endl; return; }
    rc = zmq_connect(s_conn, endpoint.c_str());
    if (rc != 0) { std::cerr << "connect failed: " << zmq_strerror(errno) << std::endl; return; }

    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(msg_size);
    stopwatch_t sw;

    // Warmup
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Latency
    int lat_count = 1000;
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        zmq_send(s_conn, buffer.data(), msg_size, 0);
        zmq_recv(s_bind, recv_buf.data(), msg_size, 0);
        zmq_send(s_bind, recv_buf.data(), msg_size, 0);
        zmq_recv(s_conn, recv_buf.data(), msg_size, 0);
    }
    double latency = (sw.elapsed_ms() * 1000.0) / (lat_count * 2);

    // Throughput
    std::thread receiver([&]() {
        for (int i = 0; i < msg_count; ++i) {
            zmq_recv(s_bind, recv_buf.data(), msg_size, 0);
        }
    });

    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        zmq_send(s_conn, buffer.data(), msg_size, 0);
    }
    receiver.join();
    double throughput = (double)msg_count / (sw.elapsed_ms() / 1000.0);

    print_result("zlink", "PAIR", transport, msg_size, throughput, latency);

    zmq_close(s_bind);
    zmq_close(s_conn);
    zmq_ctx_term(ctx);
}

int main() {
    std::cerr << "=== comp_zlink_pair starting ===" << std::endl;
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
