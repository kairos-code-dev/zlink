#include "../common/bench_common.hpp"
#include <zmq.h>
#include <thread>
#include <vector>
#include <cstring>

void run_dealer_router(const std::string& transport, size_t msg_size, int msg_count, const std::string& lib_name) {
    void *ctx = zmq_ctx_new();
    void *router = zmq_socket(ctx, ZMQ_ROUTER);
    void *dealer = zmq_socket(ctx, ZMQ_DEALER);

    // Set Routing ID for Dealer
    zmq_setsockopt(dealer, ZMQ_ROUTING_ID, "CLIENT", 6);

    std::string endpoint = make_endpoint(transport, lib_name + "_dealer_router");
    zmq_bind(router, endpoint.c_str());
    zmq_connect(dealer, endpoint.c_str());

    // Give it time to connect
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(msg_size + 256);
    char id[256];
    stopwatch_t sw;

    // --- Warmup (1,000 roundtrips) ---
    for (int i = 0; i < 1000; ++i) {
        zmq_send(dealer, buffer.data(), msg_size, 0);
        
        // Router receives: [Identity] [Data]
        int id_len = zmq_recv(router, id, 256, 0);
        zmq_recv(router, recv_buf.data(), msg_size, 0);
        
        // Router replies: [Identity] [Data]
        zmq_send(router, id, id_len, ZMQ_SNDMORE);
        zmq_send(router, buffer.data(), msg_size, 0);
        
        // Dealer receives: [Data]
        zmq_recv(dealer, recv_buf.data(), msg_size, 0);
    }

    // --- Latency (1,000 roundtrips) ---
    int lat_count = 1000;
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        zmq_send(dealer, buffer.data(), msg_size, 0);
        
        int id_len = zmq_recv(router, id, 256, 0);
        zmq_recv(router, recv_buf.data(), msg_size, 0);
        
        zmq_send(router, id, id_len, ZMQ_SNDMORE);
        zmq_send(router, buffer.data(), msg_size, 0);
        
        zmq_recv(dealer, recv_buf.data(), msg_size, 0);
    }
    double latency = (sw.elapsed_ms() * 1000.0) / (lat_count * 2);

    // --- Throughput ---
    std::thread receiver([&]() {
        char id_inner[256];
        for (int i = 0; i < msg_count; ++i) {
            zmq_recv(router, id_inner, 256, 0); // Identity
            zmq_recv(router, recv_buf.data(), msg_size, 0); // Data
        }
    });

    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        zmq_send(dealer, buffer.data(), msg_size, 0);
    }
    receiver.join();
    double throughput = (double)msg_count / (sw.elapsed_ms() / 1000.0);

    print_result(lib_name, "DEALER_ROUTER", transport, msg_size, throughput, latency);

    zmq_close(router);
    zmq_close(dealer);
    zmq_ctx_term(ctx);
}

int main(int argc, char** argv) {
    if (argc < 4) return 1;
    std::string lib_name = argv[1];
    std::string transport = argv[2];
    size_t size = std::stoul(argv[3]);
    int count = (size <= 1024) ? 50000 : 2000;
    run_dealer_router(transport, size, count, lib_name);
    return 0;
}