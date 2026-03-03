#include <zlink.hpp>

#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "perf_dispatch.hpp"

int run_pubsub(const std::string &transport, size_t size)
{
    int warmup = env_int("PERF_WARMUP_COUNT", 1000);
    int msg_count = resolve_msg_count(size);
    int sndhwm = env_int("PERF_SINGLE_SNDHWM", msg_count + warmup + 1024);
    int rcvhwm = env_int("PERF_SINGLE_RCVHWM", msg_count + warmup + 1024);
    int sndtimeo = env_int("PERF_SINGLE_SNDTIMEO_MS", 1000);
    int rcvtimeo = env_int("PERF_SINGLE_RCVTIMEO_MS", 1000);

    zlink::context_t ctx;
    zlink::socket_t pub(ctx, zlink::socket_type::pub);
    zlink::socket_t sub(ctx, zlink::socket_type::sub);

    (void) pub.set(zlink::socket_option::sndhwm, sndhwm);
    (void) sub.set(zlink::socket_option::rcvhwm, rcvhwm);
    (void) pub.set(zlink::socket_option::sndtimeo, sndtimeo);
    (void) sub.set(zlink::socket_option::rcvtimeo, rcvtimeo);
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
    std::atomic<int> recv_count(0);
    std::atomic<bool> sender_done(false);
    std::thread receiver([&]() {
        auto last_recv = std::chrono::steady_clock::now();
        while (recv_count.load() < msg_count) {
            int rc = sub.recv(rbuf.data(), size, zlink::recv_flag::dontwait);
            if (rc >= 0) {
                recv_count.fetch_add(1);
                last_recv = std::chrono::steady_clock::now();
                continue;
            }
            int err = zlink_errno();
            if (err == EAGAIN || err == EWOULDBLOCK) {
                if (sender_done.load()) {
                    auto now = std::chrono::steady_clock::now();
                    auto idle = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now - last_recv);
                    if (idle.count() > 500)
                        break;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                continue;
            }
            if (err == EINTR)
                continue;
            if (sender_done.load()) {
                break;
            } else {
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
    sender_done.store(true);
    receiver.join();

    int received = recv_count.load();
    if (!recv_ok.load() || received <= 0)
        return 2;

    double sec = static_cast<double>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - t0)
        .count()) / 1e9;
    if (sec <= 0.0)
        return 2;
    double throughput = static_cast<double>(received) / sec;
    double latency = sec * 1e6 / static_cast<double>(received);

    print_result("PUBSUB", transport, size, throughput, latency);
    return 0;
}

int run_pattern_pubsub(const std::string &transport, size_t size)
{
    return run_pubsub(transport, size);
}
