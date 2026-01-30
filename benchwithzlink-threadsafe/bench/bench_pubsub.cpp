#include "../common/bench_common.hpp"
#include <zlink.h>
#include <thread>
#include <vector>
#include <cstring>

void run_pubsub(const std::string& transport, size_t msg_size, int msg_count, const std::string& lib_name) {
    if (!transport_available(transport)) return;
    void *ctx = zlink_ctx_new();
    void *pub = bench_socket(ctx, ZLINK_PUB);
    void *sub = bench_socket(ctx, ZLINK_SUB);

    const bool is_pgm = transport == "pgm" || transport == "epgm";
    int hwm = is_pgm ? 1000 : 0;
    set_sockopt_int(pub, ZLINK_SNDHWM, hwm, "ZLINK_SNDHWM");
    set_sockopt_int(sub, ZLINK_RCVHWM, hwm, "ZLINK_RCVHWM");
    zlink_setsockopt(sub, ZLINK_SUBSCRIBE, "", 0);
    int poll_timeout_ms = 0;
    if (is_pgm) {
        poll_timeout_ms =
          resolve_bench_count("BENCH_PGM_POLL_TIMEOUT_MS", 50);
        const int linger_ms = 0;
        set_sockopt_int(pub, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
        set_sockopt_int(sub, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
        const char *cap = transport == "pgm" ? "pgm" : "epgm";
        if (!zlink_has(cap)) {
            print_result(lib_name, "PUBSUB", transport, msg_size, 0.0, 0.0);
            zlink_close(pub);
            zlink_close(sub);
            zlink_ctx_term(ctx);
            return;
        }
        const int timeout_ms = poll_timeout_ms;
        set_sockopt_int(pub, ZLINK_SNDTIMEO, timeout_ms, "ZLINK_SNDTIMEO");
        set_sockopt_int(sub, ZLINK_RCVTIMEO, timeout_ms, "ZLINK_RCVTIMEO");
    }

    if (!setup_tls_server(pub, transport) ||
        !setup_tls_client(sub, transport)) {
        zlink_close(pub);
        zlink_close(sub);
        zlink_ctx_term(ctx);
        return;
    }

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

    int warmup_count = resolve_bench_count("BENCH_WARMUP_COUNT", 1000);
    if (is_pgm) {
        const int max_count = resolve_bench_count(
          msg_size >= 65536 ? "BENCH_PGM_MSG_COUNT_LARGE"
                            : "BENCH_PGM_MSG_COUNT",
          msg_size >= 65536 ? 100 : 500);
        const int max_warmup = resolve_bench_count(
          msg_size >= 65536 ? "BENCH_PGM_WARMUP_COUNT_LARGE"
                            : "BENCH_PGM_WARMUP_COUNT",
          msg_size >= 65536 ? 10 : 50);
        if (msg_count > max_count)
            msg_count = max_count;
        if (warmup_count > max_warmup)
            warmup_count = max_warmup;
    }

    if (is_pgm) {
        for (int i = 0; i < warmup_count; ++i) {
            bench_send_fast(pub, buffer.data(), msg_size, 0, "warmup send");
            zlink_pollitem_t items[] = {{sub, 0, ZLINK_POLLIN, 0}};
            if (zlink_poll(items, 1, poll_timeout_ms) > 0
                && (items[0].revents & ZLINK_POLLIN)) {
                bench_recv_fast(sub, recv_buf.data(), msg_size, 0, "warmup recv");
            }
        }

        sw.start();
        int received = 0;
        for (int i = 0; i < msg_count; ++i) {
            bench_send_fast(pub, buffer.data(), msg_size, 0, "thr send");
            zlink_pollitem_t items[] = {{sub, 0, ZLINK_POLLIN, 0}};
            if (zlink_poll(items, 1, poll_timeout_ms) > 0
                && (items[0].revents & ZLINK_POLLIN)) {
                if (bench_recv_fast(sub, recv_buf.data(), msg_size, 0, "thr recv") >= 0)
                    ++received;
            }
        }
        double elapsed_ms = sw.elapsed_ms();
        double throughput = received > 0 ? (double)received / (elapsed_ms / 1000.0) : 0.0;
        double latency = received > 0 ? elapsed_ms * 1000.0 / received : 0.0;
        print_result(lib_name, "PUBSUB", transport, msg_size, throughput, latency);
        zlink_close(pub);
        zlink_close(sub);
        zlink_ctx_term(ctx);
        return;
    } else {
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
        bench_run_senders(msg_count, [&]() {
            bench_send_fast(pub, buffer.data(), msg_size, 0, "thr send");
        });
        receiver.join();
    }
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
