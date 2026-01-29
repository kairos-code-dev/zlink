#include "../common/bench_common.hpp"
#include <zmq.h>
#include <thread>
#include <vector>
#include <cstring>

void run_dealer_router(const std::string& transport, size_t msg_size, int msg_count, const std::string& lib_name) {
    if (!transport_available(transport)) return;
    void *ctx = zmq_ctx_new();
    apply_io_threads(ctx);
    void *router = zmq_socket(ctx, ZMQ_ROUTER);
    void *dealer = zmq_socket(ctx, ZMQ_DEALER);

    // Set Routing ID for Dealer
    zmq_setsockopt(dealer, ZMQ_ROUTING_ID, "CLIENT", 6);

    int hwm = 0;
    set_sockopt_int(router, ZMQ_SNDHWM, hwm, "ZMQ_SNDHWM");
    set_sockopt_int(router, ZMQ_RCVHWM, hwm, "ZMQ_RCVHWM");
    set_sockopt_int(dealer, ZMQ_RCVHWM, hwm, "ZMQ_RCVHWM");
    set_sockopt_int(dealer, ZMQ_SNDHWM, hwm, "ZMQ_SNDHWM");

    std::string endpoint = bind_and_resolve_endpoint(router, transport, lib_name + "_dealer_router");
    if (endpoint.empty()) {
        zmq_close(router);
        zmq_close(dealer);
        zmq_ctx_term(ctx);
        return;
    }
    if (!connect_checked(dealer, endpoint)) {
        zmq_close(router);
        zmq_close(dealer);
        zmq_ctx_term(ctx);
        return;
    }
    apply_debug_timeouts(router, transport);
    apply_debug_timeouts(dealer, transport);
    settle();

    // Give it time to connect
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(msg_size + 256);
    char id[256];
    stopwatch_t sw;

    // --- Warmup (1,000 roundtrips) ---
    const int warmup_count = resolve_bench_count("BENCH_WARMUP_COUNT", 1000);
    for (int i = 0; i < warmup_count; ++i) {
        bench_send_fast(dealer, buffer.data(), msg_size, 0, "warmup send");
        
        // Router receives: [Identity] [Data]
        int id_len = bench_recv_fast(router, id, 256, 0, "warmup recv id");
        bench_recv_fast(router, recv_buf.data(), msg_size, 0,
                        "warmup recv data");
        
        // Router replies: [Identity] [Data]
        bench_send_fast(router, id, id_len, ZMQ_SNDMORE, "warmup send id");
        bench_send_fast(router, buffer.data(), msg_size, 0,
                        "warmup send data");
        
        // Dealer receives: [Data]
        bench_recv_fast(dealer, recv_buf.data(), msg_size, 0, "warmup recv");
    }

    // --- Latency (1,000 roundtrips) ---
    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 1000);
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        bench_send_fast(dealer, buffer.data(), msg_size, 0, "lat send");
        
        int id_len = bench_recv_fast(router, id, 256, 0, "lat recv id");
        bench_recv_fast(router, recv_buf.data(), msg_size, 0, "lat recv data");
        
        bench_send_fast(router, id, id_len, ZMQ_SNDMORE, "lat send id");
        bench_send_fast(router, buffer.data(), msg_size, 0, "lat send data");
        
        bench_recv_fast(dealer, recv_buf.data(), msg_size, 0, "lat recv");
    }
    double latency = (sw.elapsed_ms() * 1000.0) / (lat_count * 2);

    // --- Throughput ---
    std::thread receiver([&]() {
        char id_inner[256];
        for (int i = 0; i < msg_count; ++i) {
            bench_recv_fast(router, id_inner, 256, 0, "thr recv id"); // Identity
            bench_recv_fast(router, recv_buf.data(), msg_size, 0,
                            "thr recv data"); // Data
        }
    });

    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        bench_send_fast(dealer, buffer.data(), msg_size, 0, "thr send");
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
    int count = resolve_msg_count(size);
    run_dealer_router(transport, size, count, lib_name);
    return 0;
}
