#include "../common/bench_common.hpp"
#include <zmq.h>
#include <thread>
#include <vector>
#include <cstring>

void run_pubsub(const std::string& transport, size_t msg_size, int msg_count, const std::string& lib_name) {
    if (!transport_available(transport)) return;
    void *ctx = zmq_ctx_new();
    void *pub = zmq_socket(ctx, ZMQ_PUB);
    void *sub = zmq_socket(ctx, ZMQ_SUB);

    int hwm = 0;
    set_sockopt_int(pub, ZMQ_SNDHWM, hwm, "ZMQ_SNDHWM");
    set_sockopt_int(sub, ZMQ_RCVHWM, hwm, "ZMQ_RCVHWM");
    zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "", 0);

    if (!setup_tls_server(pub, transport) ||
        !setup_tls_client(sub, transport)) {
        zmq_close(pub);
        zmq_close(sub);
        zmq_ctx_term(ctx);
        return;
    }

    std::string endpoint = bind_and_resolve_endpoint(pub, transport, lib_name + "_pubsub");
    if (endpoint.empty()) {
        zmq_close(pub);
        zmq_close(sub);
        zmq_ctx_term(ctx);
        return;
    }
    if (!connect_checked(sub, endpoint)) {
        zmq_close(pub);
        zmq_close(sub);
        zmq_ctx_term(ctx);
        return;
    }

    apply_debug_timeouts(pub, transport);
    apply_debug_timeouts(sub, transport);
    settle();

    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(msg_size);
    stopwatch_t sw;

    // Warmup
    const int warmup_count = resolve_bench_count("BENCH_WARMUP_COUNT", 1000);
    for (int i = 0; i < warmup_count; ++i) {
        bench_send_fast(pub, buffer.data(), msg_size, 0, "warmup send");
        bench_recv_fast(sub, recv_buf.data(), msg_size, 0, "warmup recv");
    }

    // Throughput (Simple latency for pubsub is hard, use total time / count)
    std::thread receiver([&]() {
        for (int i = 0; i < msg_count; ++i) {
            bench_recv_fast(sub, recv_buf.data(), msg_size, 0, "thr recv");
        }
    });

    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        bench_send_fast(pub, buffer.data(), msg_size, 0, "thr send");
    }
    receiver.join();
    double throughput = (double)msg_count / (sw.elapsed_ms() / 1000.0);
    double latency = sw.elapsed_ms() * 1000.0 / msg_count;

    print_result(lib_name, "PUBSUB", transport, msg_size, throughput, latency);

    zmq_close(pub);
    zmq_close(sub);
    zmq_ctx_term(ctx);
}

int main(int argc, char** argv) {
    if (argc < 4) return 1;
    std::string lib_name = argv[1];
    std::string transport = argv[2];
    size_t size = std::stoul(argv[3]);
    int count = resolve_msg_count(size);
    run_pubsub(transport, size, count, lib_name);
    return 0;
}
