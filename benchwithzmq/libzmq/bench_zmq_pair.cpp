#include "../common/bench_common.hpp"
#include <zmq.h>
#include <thread>
#include <vector>
#include <cstring>

#ifndef ZMQ_TCP_NODELAY
#define ZMQ_TCP_NODELAY 26
#endif

void run_pair(const std::string& transport, size_t msg_size, int msg_count, const std::string& lib_name) {
    if (!transport_available(transport)) return;
    void *ctx = zmq_ctx_new();
    void *s_bind = zmq_socket(ctx, ZMQ_PAIR);
    void *s_conn = zmq_socket(ctx, ZMQ_PAIR);

    int nodelay = 1;
    zmq_setsockopt(s_bind, ZMQ_TCP_NODELAY, &nodelay, sizeof(nodelay));
    zmq_setsockopt(s_conn, ZMQ_TCP_NODELAY, &nodelay, sizeof(nodelay));

    int hwm = 0; 
    zmq_setsockopt(s_bind, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(s_bind, ZMQ_RCVHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(s_conn, ZMQ_RCVHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(s_conn, ZMQ_SNDHWM, &hwm, sizeof(hwm));

    std::string endpoint = bind_and_resolve_endpoint(s_bind, transport, lib_name + "_pair");
    if (endpoint.empty()) {
        zmq_close(s_bind);
        zmq_close(s_conn);
        zmq_ctx_term(ctx);
        return;
    }
    if (!connect_checked(s_conn, endpoint)) {
        zmq_close(s_bind);
        zmq_close(s_conn);
        zmq_ctx_term(ctx);
        return;
    }
    settle();

    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(msg_size);
    stopwatch_t sw;

    for (int i = 0; i < 1000; ++i) {
        zmq_send(s_conn, buffer.data(), msg_size, 0);
        zmq_recv(s_bind, recv_buf.data(), msg_size, 0);
    }

    int lat_count = 500;
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        zmq_send(s_conn, buffer.data(), msg_size, 0);
        zmq_recv(s_bind, recv_buf.data(), msg_size, 0);
        zmq_send(s_bind, recv_buf.data(), msg_size, 0);
        zmq_recv(s_conn, recv_buf.data(), msg_size, 0);
    }
    double latency = (sw.elapsed_ms() * 1000.0) / (lat_count * 2);

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

    print_result(lib_name, "PAIR", transport, msg_size, throughput, latency);

    zmq_close(s_bind);
    zmq_close(s_conn);
    zmq_ctx_term(ctx);
}

int main(int argc, char** argv) {
    if (argc < 4) return 1;
    std::string lib_name = argv[1];
    std::string transport = argv[2];
    size_t size = std::stoul(argv[3]);
    int count = resolve_msg_count(size);
    run_pair(transport, size, count, lib_name);
    return 0;
}
