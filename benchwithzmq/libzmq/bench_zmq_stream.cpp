#include "../common/bench_common.hpp"
#include <zmq.h>
#include <thread>
#include <vector>
#include <cstring>

#ifndef ZMQ_STREAM
#define ZMQ_STREAM 11
#endif

#ifndef ZMQ_DONTWAIT
#define ZMQ_DONTWAIT 1
#endif

#ifndef EAGAIN
#define EAGAIN 11
#endif

void run_stream(const std::string& transport, size_t msg_size, int msg_count, const std::string& lib_name) {
    void *ctx = zmq_ctx_new();
    void *stream = zmq_socket(ctx, ZMQ_STREAM);
    void *dealer = zmq_socket(ctx, ZMQ_DEALER);

    // Socket settings
    int hwm = 0;
    zmq_setsockopt(stream, ZMQ_SNDHWM, &hwm, sizeof(hwm));
    zmq_setsockopt(dealer, ZMQ_RCVHWM, &hwm, sizeof(hwm));

    // Bind STREAM, connect DEALER - USE TCP
    std::string endpoint = make_endpoint(transport, lib_name + "_stream");
    zmq_bind(stream, endpoint.c_str());
    zmq_connect(dealer, endpoint.c_str());

    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(msg_size);
    std::vector<char> id_buf(256);
    std::vector<char> routing_id;

    stopwatch_t sw;

    // Wait for connection
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Send first message
    zmq_send(dealer, buffer.data(), msg_size, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Receive routing_id and data
    int id_len = zmq_recv(stream, id_buf.data(), 256, 0);
    if (id_len > 0) {
        routing_id.assign(id_buf.begin(), id_buf.begin() + id_len);
    }
    zmq_recv(stream, recv_buf.data(), msg_size, 0);

    // Warmup
    for (int i = 0; i < 1000; ++i) {
        zmq_send(dealer, buffer.data(), msg_size, 0);
        zmq_recv(stream, id_buf.data(), 256, 0);  // routing_id
        zmq_recv(stream, recv_buf.data(), msg_size, 0);  // data
    }

    // Latency test
    int lat_count = 500;
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        // DEALER -> STREAM
        zmq_send(dealer, buffer.data(), msg_size, 0);
        zmq_recv(stream, id_buf.data(), 256, 0);
        zmq_recv(stream, recv_buf.data(), msg_size, 0);

        // STREAM -> DEALER
        zmq_send(stream, routing_id.data(), routing_id.size(), ZMQ_SNDMORE);
        zmq_send(stream, recv_buf.data(), msg_size, 0);
        zmq_recv(dealer, recv_buf.data(), msg_size, 0);
    }
    double latency = (sw.elapsed_ms() * 1000.0) / (lat_count * 2);

    // Throughput test
    std::thread receiver([&]() {
        for (int i = 0; i < msg_count; ++i) {
            zmq_recv(stream, id_buf.data(), 256, 0);
            zmq_recv(stream, recv_buf.data(), msg_size, 0);
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        zmq_send(dealer, buffer.data(), msg_size, 0);
    }
    receiver.join();
    double throughput = (double)msg_count / (sw.elapsed_ms() / 1000.0);

    print_result(lib_name, "STREAM", transport, msg_size, throughput, latency);

    zmq_close(dealer);
    zmq_close(stream);
    zmq_ctx_term(ctx);
}

int main(int argc, char** argv) {
    if (argc < 4) return 1;
    std::string lib_name = argv[1];
    std::string transport = argv[2];
    size_t size = std::stoul(argv[3]);
    int count = (size <= 1024) ? 200000 : 20000;
    run_stream(transport, size, count, lib_name);
    return 0;
}
