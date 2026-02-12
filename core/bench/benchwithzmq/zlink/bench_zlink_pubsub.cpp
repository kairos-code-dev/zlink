#include "../common/bench_common_zlink.hpp"
#include <zlink.h>
#include <vector>
#include <cstring>

void run_pubsub(const std::string& transport, size_t msg_size, int msg_count, const std::string& lib_name) {
    if (!transport_available(transport))
        return;

    ctx_guard_t ctx;
    if (!ctx.valid())
        return;

    socket_guard_t pub(ctx.get(), ZLINK_PUB);
    socket_guard_t sub(ctx.get(), ZLINK_SUB);
    if (!pub.valid() || !sub.valid())
        return;

    const bool is_pgm = transport == "pgm" || transport == "epgm";
    zlink_setsockopt(sub.get(), ZLINK_SUBSCRIBE, "", 0);
    int poll_timeout_ms = 0;
    if (is_pgm) {
        poll_timeout_ms =
          resolve_bench_count("BENCH_PGM_POLL_TIMEOUT_MS", 50);
        const int linger_ms = 0;
        set_sockopt_int(pub.get(), ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
        set_sockopt_int(sub.get(), ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
        const char *cap = transport == "pgm" ? "pgm" : "epgm";
        if (!zlink_has(cap)) {
            print_result(lib_name, "PUBSUB", transport, msg_size, 0.0, 0.0);
            return;
        }
        const int timeout_ms = poll_timeout_ms;
        set_sockopt_int(pub.get(), ZLINK_SNDTIMEO, timeout_ms, "ZLINK_SNDTIMEO");
        set_sockopt_int(sub.get(), ZLINK_RCVTIMEO, timeout_ms, "ZLINK_RCVTIMEO");
    }

    if (!setup_connected_pair(pub.get(), sub.get(), transport,
                              lib_name + "_pubsub")) {
        return;
    }

    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(msg_size);

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

    if (!is_pgm) {
        poll_timeout_ms = resolve_bench_count("BENCH_PUBSUB_POLL_TIMEOUT_MS", 50);
    }

    repeat_n(warmup_count, [&]() {
        zlink_send(pub.get(), buffer.data(), msg_size, 0);
        zlink_pollitem_t items[] = {{sub.get(), 0, ZLINK_POLLIN, 0}};
        if (zlink_poll(items, 1, poll_timeout_ms) > 0
            && (items[0].revents & ZLINK_POLLIN)) {
            zlink_recv(sub.get(), recv_buf.data(), msg_size, 0);
        }
    });

    stopwatch_t sw;
    sw.start();
    int received = 0;
    for (int i = 0; i < msg_count; ++i) {
        zlink_send(pub.get(), buffer.data(), msg_size, 0);
        zlink_pollitem_t items[] = {{sub.get(), 0, ZLINK_POLLIN, 0}};
        if (zlink_poll(items, 1, poll_timeout_ms) > 0
            && (items[0].revents & ZLINK_POLLIN)) {
            if (zlink_recv(sub.get(), recv_buf.data(), msg_size, 0) >= 0)
                ++received;
        }
    }

    const double elapsed_ms = sw.elapsed_ms();
    const double throughput =
      received > 0 ? (double)received / (elapsed_ms / 1000.0) : 0.0;
    const double latency = received > 0 ? elapsed_ms * 1000.0 / received : 0.0;
    print_result(lib_name, "PUBSUB", transport, msg_size, throughput, latency);
}

int main(int argc, char** argv) {
    return run_standard_bench_main(argc, argv, run_pubsub);
}
