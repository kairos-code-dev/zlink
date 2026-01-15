#include "../common/bench_common.hpp"
#include <zmq.h>
#include <thread>
#include <vector>
#include <cstring>
#include <cstdlib>

#ifndef ZMQ_TCP_NODELAY
#define ZMQ_TCP_NODELAY 26
#endif

void run_pair(const std::string& transport, size_t msg_size, int msg_count, const std::string& lib_name) {
    if (!transport_available(transport)) return;
    void *ctx = zmq_ctx_new();
    void *s_bind = zmq_socket(ctx, ZMQ_PAIR);
    void *s_conn = zmq_socket(ctx, ZMQ_PAIR);

    int nodelay = 1;
    set_sockopt_int(s_bind, ZMQ_TCP_NODELAY, nodelay, "ZMQ_TCP_NODELAY");
    set_sockopt_int(s_conn, ZMQ_TCP_NODELAY, nodelay, "ZMQ_TCP_NODELAY");

    int hwm = 0; 
    set_sockopt_int(s_bind, ZMQ_SNDHWM, hwm, "ZMQ_SNDHWM");
    set_sockopt_int(s_bind, ZMQ_RCVHWM, hwm, "ZMQ_RCVHWM");
    set_sockopt_int(s_conn, ZMQ_RCVHWM, hwm, "ZMQ_RCVHWM");
    set_sockopt_int(s_conn, ZMQ_SNDHWM, hwm, "ZMQ_SNDHWM");

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
    apply_debug_timeouts(s_bind, transport);
    apply_debug_timeouts(s_conn, transport);
    settle();

    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(msg_size);
    stopwatch_t sw;

    const int warmup_count = resolve_bench_count("BENCH_WARMUP_COUNT", 1000);
    for (int i = 0; i < warmup_count; ++i) {
        bench_send(s_conn, buffer.data(), msg_size, 0, "warmup send");
        bench_recv(s_bind, recv_buf.data(), msg_size, 0, "warmup recv");
    }

    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 500);
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        bench_send(s_conn, buffer.data(), msg_size, 0, "lat send");
        bench_recv(s_bind, recv_buf.data(), msg_size, 0, "lat recv");
        bench_send(s_bind, recv_buf.data(), msg_size, 0, "lat send back");
        bench_recv(s_conn, recv_buf.data(), msg_size, 0, "lat recv back");
    }
    double latency = (sw.elapsed_ms() * 1000.0) / (lat_count * 2);

    std::thread receiver([&]() {
        for (int i = 0; i < msg_count; ++i) {
            bench_recv(s_bind, recv_buf.data(), msg_size, 0, "thr recv");
        }
    });

    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        bench_send(s_conn, buffer.data(), msg_size, 0, "thr send");
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
