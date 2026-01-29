#include "../common/bench_common.hpp"
#include <zlink.h>
#include <thread>
#include <vector>
#include <cstring>

void run_pubsub(const std::string& transport, size_t msg_size, int msg_count, const std::string& lib_name) {
    if (!transport_available(transport)) return;
    void *ctx = zlink_ctx_new();
    void *pub = zlink_socket(ctx, ZLINK_PUB);
    void *sub = zlink_socket(ctx, ZLINK_SUB);

    int hwm = 0;
    set_sockopt_int(pub, ZLINK_SNDHWM, hwm, "ZLINK_SNDHWM");
    set_sockopt_int(sub, ZLINK_RCVHWM, hwm, "ZLINK_RCVHWM");
    zlink_setsockopt(sub, ZLINK_SUBSCRIBE, "", 0);

    std::string endpoint = bind_and_resolve_endpoint(pub, transport, lib_name + "_pubsub");
    if (endpoint.empty()) {
        zlink_close(pub);
        zlink_close(sub);
        zlink_ctx_term(ctx);
        return;
    }
    if (!connect_checked(sub, endpoint)) {
        zlink_close(pub);
        zlink_close(sub);
        zlink_ctx_term(ctx);
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

    zlink_close(pub);
    zlink_close(sub);
    zlink_ctx_term(ctx);
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
