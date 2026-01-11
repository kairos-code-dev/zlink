#include "../common/bench_common.hpp"
#include <zmq.h>
#include <thread>
#include <vector>
#include <cstring>

void run_pubsub(const std::string& transport, size_t msg_size, int msg_count, const std::string& lib_name) {
    void *ctx = zmq_ctx_new();
    void *pub = zmq_socket(ctx, ZMQ_PUB);
    void *sub = zmq_socket(ctx, ZMQ_SUB);

    int hwm = 0;
    zmq_setsockopt(pub, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(sub, ZMQ_RCVHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "", 0);

    std::string endpoint = make_endpoint(transport, lib_name + "_pubsub");
    zmq_bind(pub, endpoint.c_str());
    zmq_connect(sub, endpoint.c_str());

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(msg_size);
    stopwatch_t sw;

    // Warmup
    for (int i = 0; i < 1000; ++i) {
        zmq_send(pub, buffer.data(), msg_size, 0);
        zmq_recv(sub, recv_buf.data(), msg_size, 0);
    }

    // Throughput (Simple latency for pubsub is hard, use total time / count)
    std::thread receiver([&]() {
        for (int i = 0; i < msg_count; ++i) {
            zmq_recv(sub, recv_buf.data(), msg_size, 0);
        }
    });

    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        zmq_send(pub, buffer.data(), msg_size, 0);
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
    int count = (size <= 1024) ? 100000 : 2000;
    run_pubsub(transport, size, count, lib_name);
    return 0;
}