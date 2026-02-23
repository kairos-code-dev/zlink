#include <zlink.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "pattern_dispatch.hpp"

int run_pattern_dealer_dealer(const std::string &transport, size_t size)
{
    int warmup = env_int("BENCH_WARMUP_COUNT", 1000);
    int lat_count = env_int("BENCH_LAT_COUNT", 500);
    int msg_count = resolve_msg_count(size);

    zlink::context_t ctx;
    zlink::socket_t a(ctx, zlink::socket_type::dealer);
    zlink::socket_t b(ctx, zlink::socket_type::dealer);

    std::string endpoint = endpoint_for(transport, "dealer_dealer");
    if (a.bind(endpoint) != 0 || b.connect(endpoint) != 0)
        return 2;

    settle();

    std::vector<char> buf(size, 'a');
    std::vector<char> rbuf(size > 256 ? size : 256);

    for (int i = 0; i < warmup; ++i) {
        if (b.send(buf.data(), size) < 0 || a.recv(rbuf.data(), size) < 0)
            return 2;
    }

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < lat_count; ++i) {
        if (b.send(buf.data(), size) < 0)
            return 2;
        int n = a.recv(rbuf.data(), size);
        if (n < 0)
            return 2;
        if (a.send(rbuf.data(), static_cast<size_t>(n)) < 0 || b.recv(rbuf.data(), size) < 0)
            return 2;
    }

    double latency =
      static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count())
      / (lat_count * 2);

    std::atomic<bool> recv_ok(true);
    std::thread receiver([&]() {
        for (int i = 0; i < msg_count; ++i) {
            if (a.recv(rbuf.data(), size) < 0) {
                recv_ok.store(false);
                return;
            }
        }
    });

    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < msg_count; ++i) {
        if (b.send(buf.data(), size) < 0) {
            recv_ok.store(false);
            break;
        }
    }
    receiver.join();

    if (!recv_ok.load())
        return 2;

    double throughput =
      static_cast<double>(msg_count)
      / (static_cast<double>(
           std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now() - t0)
             .count())
         / 1e9);

    print_result("DEALER_DEALER", transport, size, throughput, latency);
    return 0;
}
