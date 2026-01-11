#include "../common/bench_common.hpp"
#include <zmq.h>
#include <thread>
#include <vector>
#include <cstring>
#include <iostream>

void run_router_router(const std::string& transport, size_t msg_size, int msg_count, const std::string& lib_name) {
    void *ctx = zmq_ctx_new();
    void *router1 = zmq_socket(ctx, ZMQ_ROUTER);
    void *router2 = zmq_socket(ctx, ZMQ_ROUTER);

    // Set Routing IDs
    zmq_setsockopt(router1, ZMQ_ROUTING_ID, "ROUTER1", 7);
    zmq_setsockopt(router2, ZMQ_ROUTING_ID, "ROUTER2", 7);

    // Make unroutable messages fail instead of silent drop (for debugging safety)
    int mandatory = 1;
    zmq_setsockopt(router1, ZMQ_ROUTER_MANDATORY, &mandatory, sizeof(mandatory));
    zmq_setsockopt(router2, ZMQ_ROUTER_MANDATORY, &mandatory, sizeof(mandatory));

    std::string endpoint = make_endpoint(transport, lib_name + "_router_router");
    zmq_bind(router1, endpoint.c_str());
    zmq_connect(router2, endpoint.c_str());

    // --- HANDSHAKE PHASE (Crucial for Router-Router) ---
    // Router drops messages if connection isn't fully established.
    // We loop until we successfully exchange a 'PING' message.
    bool connected = false;
    char buf[16];
    
    // R2 tries to ping R1 until success
    while (!connected) {
        // Try sending PING from R2 to R1
        zmq_send(router2, "ROUTER1", 7, ZMQ_SNDMORE | ZMQ_DONTWAIT);
        int rc = zmq_send(router2, "PING", 4, ZMQ_DONTWAIT);
        
        if (rc == 4) {
            // Check if R1 received it
            int len = zmq_recv(router1, buf, 16, ZMQ_DONTWAIT);
            if (len > 0) { // Received ID
                zmq_recv(router1, buf, 16, 0); // Receive "PING"
                connected = true;
            }
        }
        if (!connected) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // Reply PONG to complete handshake
    zmq_send(router1, "ROUTER2", 7, ZMQ_SNDMORE);
    zmq_send(router1, "PONG", 4, 0);
    
    zmq_recv(router2, buf, 16, 0); // ID
    zmq_recv(router2, buf, 16, 0); // PONG

    // --- BENCHMARK PHASE ---
    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(msg_size + 256);
    char id[256];
    stopwatch_t sw;

    // Latency Test (1,000 roundtrips)
    int lat_count = 1000;
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        // R2 -> R1
        zmq_send(router2, "ROUTER1", 7, ZMQ_SNDMORE);
        zmq_send(router2, buffer.data(), msg_size, 0);
        
        // R1 Recv
        int id_len = zmq_recv(router1, id, 256, 0); 
        zmq_recv(router1, recv_buf.data(), msg_size, 0);
        
        // R1 -> R2 (Reply)
        zmq_send(router1, id, id_len, ZMQ_SNDMORE);
        zmq_send(router1, buffer.data(), msg_size, 0);
        
        // R2 Recv
        zmq_recv(router2, id, 256, 0);
        zmq_recv(router2, recv_buf.data(), msg_size, 0);
    }
    double latency = (sw.elapsed_ms() * 1000.0) / (lat_count * 2);

    // Throughput Test
    std::thread receiver([&]() {
        char id_inner[256];
        for (int i = 0; i < msg_count; ++i) {
            zmq_recv(router1, id_inner, 256, 0);
            zmq_recv(router1, recv_buf.data(), msg_size, 0);
        }
    });

    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        // We use zmq_send_const if available for zero-copy like behavior on send,
        // but here standard send is fine.
        zmq_send(router2, "ROUTER1", 7, ZMQ_SNDMORE);
        zmq_send(router2, buffer.data(), msg_size, 0);
    }
    receiver.join();
    double throughput = (double)msg_count / (sw.elapsed_ms() / 1000.0);

    print_result(lib_name, "ROUTER_ROUTER", transport, msg_size, throughput, latency);

    zmq_close(router1);
    zmq_close(router2);
    zmq_ctx_term(ctx);
}

int main(int argc, char** argv) {
    if (argc < 4) return 1;
    std::string lib_name = argv[1];
    std::string transport = argv[2];
    size_t size = std::stoul(argv[3]);
    int count = (size <= 1024) ? 200000 : 20000;
    run_router_router(transport, size, count, lib_name);
    return 0;
}