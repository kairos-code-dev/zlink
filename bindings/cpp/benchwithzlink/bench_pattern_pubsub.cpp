#include <zlink.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "pattern_dispatch.hpp"

int run_pubsub(const std::string &transport, size_t size)
{
    int warmup = env_int("BENCH_WARMUP_COUNT", 1000);
    int msg_count = resolve_msg_count(size);

    zlink::context_t ctx;
    zlink::socket_t pub(ctx, zlink::socket_type::pub);
    zlink::socket_t sub(ctx, zlink::socket_type::sub);

    if (sub.set(zlink::socket_option::subscribe, "", 0) != 0)
        return 2;

    std::string endpoint = endpoint_for(transport, "pubsub");
    if (pub.bind(endpoint) != 0 || sub.connect(endpoint) != 0)
        return 2;

    settle();

    std::vector<char> buf(size, 'a');
    std::vector<char> rbuf(size > 256 ? size : 256);

    for (int i = 0; i < warmup; ++i) {
        if (pub.send(buf.data(), size) < 0 || sub.recv(rbuf.data(), size) < 0)
            return 2;
    }

    std::atomic<bool> recv_ok(true);
    std::thread receiver([&]() {
        for (int i = 0; i < msg_count; ++i) {
            if (sub.recv(rbuf.data(), size) < 0) {
                recv_ok.store(false);
                return;
            }
        }
    });

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < msg_count; ++i) {
        if (pub.send(buf.data(), size) < 0) {
            recv_ok.store(false);
            break;
        }
    }
    receiver.join();

    if (!recv_ok.load())
        return 2;

    double sec = static_cast<double>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - t0)
        .count()) / 1e9;
    double throughput = static_cast<double>(msg_count) / sec;
    double latency = sec * 1e6 / msg_count;

    print_result("PUBSUB", transport, size, throughput, latency);
    return 0;
}

int run_pattern_pubsub(const std::string &transport, size_t size)
{
    return run_pubsub(transport, size);
}
