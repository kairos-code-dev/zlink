#include <zlink.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "pattern_dispatch.hpp"

int run_dealer_router(const std::string &transport, size_t size)
{
    int warmup = env_int("BENCH_WARMUP_COUNT", 1000);
    int lat_count = env_int("BENCH_LAT_COUNT", 1000);
    int msg_count = resolve_msg_count(size);

    zlink::context_t ctx;
    zlink::socket_t router(ctx, zlink::socket_type::router);
    zlink::socket_t dealer(ctx, zlink::socket_type::dealer);

    if (dealer.set(zlink::socket_option::routing_id, "CLIENT", 6) != 0)
        return 2;

    std::string endpoint = endpoint_for(transport, "dealer_router");
    if (router.bind(endpoint) != 0 || dealer.connect(endpoint) != 0)
        return 2;

    settle();

    std::vector<char> buf(size, 'a');
    std::vector<char> recv_buf(size > 256 ? size : 256);
    char rid[256];

    for (int i = 0; i < warmup; ++i) {
        if (dealer.send(buf.data(), size) < 0)
            return 2;
        int rid_len = router.recv(rid, sizeof(rid));
        if (rid_len < 0 || router.recv(recv_buf.data(), size) < 0)
            return 2;
        if (router.send(rid, static_cast<size_t>(rid_len), zlink::send_flag::sndmore) < 0
            || router.send(buf.data(), size) < 0
            || dealer.recv(recv_buf.data(), size) < 0)
            return 2;
    }

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < lat_count; ++i) {
        if (dealer.send(buf.data(), size) < 0)
            return 2;

        int rid_len = router.recv(rid, sizeof(rid));
        if (rid_len < 0 || router.recv(recv_buf.data(), size) < 0)
            return 2;

        if (router.send(rid, static_cast<size_t>(rid_len), zlink::send_flag::sndmore) < 0
            || router.send(buf.data(), size) < 0
            || dealer.recv(recv_buf.data(), size) < 0)
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
            if (router.recv(rid, sizeof(rid)) < 0 || router.recv(recv_buf.data(), size) < 0) {
                recv_ok.store(false);
                return;
            }
        }
    });

    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < msg_count; ++i) {
        if (dealer.send(buf.data(), size) < 0) {
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

    print_result("DEALER_ROUTER", transport, size, throughput, latency);
    return 0;
}

int run_pattern_dealer_router(const std::string &transport, size_t size)
{
    return run_dealer_router(transport, size);
}
