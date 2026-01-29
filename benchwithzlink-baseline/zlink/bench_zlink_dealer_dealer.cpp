#include "../common/bench_common.hpp"
#include <zlink.h>
#include <thread>
#include <vector>
#include <cstring>

void run_dealer_dealer(const std::string& transport, size_t msg_size, int msg_count, const std::string& lib_name) {
    if (!transport_available(transport)) return;
    void *ctx = zlink_ctx_new();
    void *s1 = zlink_socket(ctx, ZLINK_DEALER);
    void *s2 = zlink_socket(ctx, ZLINK_DEALER);

    int hwm = 0;
    set_sockopt_int(s1, ZLINK_SNDHWM, hwm, "ZLINK_SNDHWM");
    set_sockopt_int(s1, ZLINK_RCVHWM, hwm, "ZLINK_RCVHWM");
    set_sockopt_int(s2, ZLINK_RCVHWM, hwm, "ZLINK_RCVHWM");
    set_sockopt_int(s2, ZLINK_SNDHWM, hwm, "ZLINK_SNDHWM");

    std::string endpoint = bind_and_resolve_endpoint(s1, transport, lib_name + "_dealer_dealer");
    if (endpoint.empty()) {
        zlink_close(s1);
        zlink_close(s2);
        zlink_ctx_term(ctx);
        return;
    }
    if (!connect_checked(s2, endpoint)) {
        zlink_close(s1);
        zlink_close(s2);
        zlink_ctx_term(ctx);
        return;
    }
    apply_debug_timeouts(s1, transport);
    apply_debug_timeouts(s2, transport);
    settle();

    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(msg_size);
    stopwatch_t sw;

    // Warmup
    const int warmup_count = resolve_bench_count("BENCH_WARMUP_COUNT", 1000);
    for (int i = 0; i < warmup_count; ++i) {
        bench_send_fast(s2, buffer.data(), msg_size, 0, "warmup send");
        bench_recv_fast(s1, recv_buf.data(), msg_size, 0, "warmup recv");
    }

    // Latency
    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 500);
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        bench_send_fast(s2, buffer.data(), msg_size, 0, "lat send");
        bench_recv_fast(s1, recv_buf.data(), msg_size, 0, "lat recv");
        bench_send_fast(s1, recv_buf.data(), msg_size, 0,
                        "lat send back");
        bench_recv_fast(s2, recv_buf.data(), msg_size, 0,
                        "lat recv back");
    }
    double latency = (sw.elapsed_ms() * 1000.0) / (lat_count * 2);

    // Throughput
    std::thread receiver([&]() {
        for (int i = 0; i < msg_count; ++i) {
            bench_recv_fast(s1, recv_buf.data(), msg_size, 0, "thr recv");
        }
    });

    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        bench_send_fast(s2, buffer.data(), msg_size, 0, "thr send");
    }
    receiver.join();
    double throughput = (double)msg_count / (sw.elapsed_ms() / 1000.0);

    print_result(lib_name, "DEALER_DEALER", transport, msg_size, throughput, latency);

    zlink_close(s1);
    zlink_close(s2);
    zlink_ctx_term(ctx);
}

int main(int argc, char** argv) {
    if (argc < 4) return 1;
    std::string lib_name = argv[1];
    std::string transport = argv[2];
    size_t size = std::stoul(argv[3]);
    int count = resolve_msg_count(size);
    run_dealer_dealer(transport, size, count, lib_name);
    return 0;
}
