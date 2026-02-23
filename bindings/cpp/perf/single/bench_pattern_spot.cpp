#include <zlink.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "pattern_dispatch.hpp"

static int spot_send_one(zlink::spot_t &spot_pub, size_t size)
{
    std::vector<zlink::message_t> parts;
    parts.emplace_back(size);
    if (size > 0)
        std::memset(parts[0].data(), 'a', size);
    return spot_pub.publish("bench", parts);
}

static int spot_recv_one(zlink::spot_t &spot_sub)
{
    zlink::msgv_t parts;
    std::string topic;
    const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
    while (std::chrono::steady_clock::now() < deadline) {
        if (spot_sub.recv(parts, topic, zlink::recv_flag::dontwait) == 0)
            return 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return -1;
}

int run_spot(const std::string &transport, size_t size)
{
    int warmup = env_int("BENCH_WARMUP_COUNT", 200);
    int lat_count = env_int("BENCH_LAT_COUNT", 200);
    int msg_count = resolve_msg_count(size);
    int max_spot = env_int("BENCH_SPOT_MSG_COUNT_MAX", 50000);
    if (msg_count > max_spot)
        msg_count = max_spot;

    zlink::context_t ctx;
    zlink::spot_node_t node_pub(ctx);
    zlink::spot_node_t node_sub(ctx);

    const std::string endpoint = endpoint_for(transport, "spot");
    if (node_pub.bind(endpoint.c_str()) != 0 || node_sub.connect_peer_pub(endpoint.c_str()) != 0)
        return 2;

    zlink::spot_t spot_pub(node_pub);
    zlink::spot_t spot_sub(node_sub);
    if (spot_sub.subscribe("bench") != 0)
        return 2;

    settle();

    for (int i = 0; i < warmup; ++i) {
        if (spot_send_one(spot_pub, size) != 0 || spot_recv_one(spot_sub) != 0)
            return 2;
    }

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < lat_count; ++i) {
        if (spot_send_one(spot_pub, size) != 0 || spot_recv_one(spot_sub) != 0)
            return 2;
    }
    double latency = static_cast<double>(
      std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0).count()) / lat_count;

    std::atomic<int> recv_count(0);
    std::thread rcv([&]() {
        for (int i = 0; i < msg_count; ++i) {
            if (spot_recv_one(spot_sub) != 0)
                break;
            ++recv_count;
        }
    });

    int sent = 0;
    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < msg_count; ++i) {
        if (spot_send_one(spot_pub, size) != 0)
            break;
        ++sent;
    }
    rcv.join();
    const int eff = sent < recv_count.load() ? sent : recv_count.load();
    const double elapsed_s = static_cast<double>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - t0).count()) / 1e9;
    const double throughput = (eff > 0 && elapsed_s > 0.0) ? (eff / elapsed_s) : 0.0;

    print_result("SPOT", transport, size, throughput, latency);
    return 0;
}

int run_pattern_spot(const std::string &transport, size_t size)
{
    return run_spot(transport, size);
}
