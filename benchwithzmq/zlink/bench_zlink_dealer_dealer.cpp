#include "../common/bench_common.hpp"
#include <zmq.h>
#include <thread>
#include <vector>
#include <cstring>

void run_dealer_dealer(const std::string& transport, size_t msg_size, int msg_count, const std::string& lib_name) {
    void *ctx = zmq_ctx_new();
    void *s1 = zmq_socket(ctx, ZMQ_DEALER);
    void *s2 = zmq_socket(ctx, ZMQ_DEALER);

    int hwm = 0;
    zmq_setsockopt(s1, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(s2, ZMQ_RCVHWM, &hwm, sizeof(hwm));

    std::string endpoint = make_endpoint(transport, lib_name + "_dealer_dealer");
    zmq_bind(s1, endpoint.c_str());
    zmq_connect(s2, endpoint.c_str());

    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(msg_size);
    stopwatch_t sw;

    // Warmup
    for (int i = 0; i < 1000; ++i) {
        zmq_send(s2, buffer.data(), msg_size, 0);
        zmq_recv(s1, recv_buf.data(), msg_size, 0);
    }

    // Latency
    int lat_count = 500;
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        zmq_send(s2, buffer.data(), msg_size, 0);
        zmq_recv(s1, recv_buf.data(), msg_size, 0);
        zmq_send(s1, recv_buf.data(), msg_size, 0);
        zmq_recv(s2, recv_buf.data(), msg_size, 0);
    }
    double latency = (sw.elapsed_ms() * 1000.0) / (lat_count * 2);

    // Throughput
    std::thread receiver([&]() {
        for (int i = 0; i < msg_count; ++i) {
            zmq_recv(s1, recv_buf.data(), msg_size, 0);
        }
    });

    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        zmq_send(s2, buffer.data(), msg_size, 0);
    }
    receiver.join();
    double throughput = (double)msg_count / (sw.elapsed_ms() / 1000.0);

    print_result(lib_name, "DEALER_DEALER", transport, msg_size, throughput, latency);

    zmq_close(s1);
    zmq_close(s2);
    zmq_ctx_term(ctx);
}

int main(int argc, char** argv) {
    if (argc < 4) return 1;
    std::string lib_name = argv[1];
    std::string transport = argv[2];
    size_t size = std::stoul(argv[3]);
    int count = (size <= 1024) ? 200000 : 20000;
    run_dealer_dealer(transport, size, count, lib_name);
    return 0;
}
