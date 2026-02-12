#include <zlink.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "pattern_dispatch.hpp"

int run_router_router(const std::string &transport, size_t size, bool use_poll)
{
    int lat_count = env_int("BENCH_LAT_COUNT", 1000);
    int msg_count = resolve_msg_count(size);

    zlink::context_t ctx;
    zlink::socket_t router1(ctx, zlink::socket_type::router);
    zlink::socket_t router2(ctx, zlink::socket_type::router);

    if (router1.set(zlink::socket_option::routing_id, "ROUTER1", 7) != 0
        || router2.set(zlink::socket_option::routing_id, "ROUTER2", 7) != 0)
        return 2;
    if (router1.set(zlink::socket_option::router_mandatory, 1) != 0
        || router2.set(zlink::socket_option::router_mandatory, 1) != 0)
        return 2;

    std::string endpoint = endpoint_for(transport, "router_router");
    if (router1.bind(endpoint) != 0 || router2.connect(endpoint) != 0)
        return 2;

    settle();

    char buf16[16];
    bool connected = false;
    for (int i = 0; i < 100; ++i) {
        router2.send("ROUTER1", 7, zlink::send_flag::sndmore | zlink::send_flag::dontwait);
        int rc = router2.send("PING", 4, zlink::send_flag::dontwait);
        if (rc == 4) {
            if (!use_poll || wait_for_input(router1, 0)) {
                int id_len = router1.recv(buf16, sizeof(buf16), zlink::recv_flag::dontwait);
                if (id_len > 0) {
                    router1.recv(buf16, sizeof(buf16), zlink::recv_flag::dontwait);
                    connected = true;
                    break;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!connected)
        return 2;

    if (router1.send("ROUTER2", 7, zlink::send_flag::sndmore) < 0
        || router1.send("PONG", 4) < 0)
        return 2;

    if (use_poll && !wait_for_input(router2, 2000))
        return 2;
    if (router2.recv(buf16, sizeof(buf16)) < 0 || router2.recv(buf16, sizeof(buf16)) < 0)
        return 2;

    std::vector<char> buffer(size, 'a');
    std::vector<char> recv_buf(size > 256 ? size : 256);
    char rid[256];

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < lat_count; ++i) {
        if (router2.send("ROUTER1", 7, zlink::send_flag::sndmore) < 0
            || router2.send(buffer.data(), size) < 0)
            return 2;

        if (use_poll && !wait_for_input(router1, 2000))
            return 2;

        int rid_len = router1.recv(rid, sizeof(rid));
        if (rid_len < 0 || router1.recv(recv_buf.data(), size) < 0)
            return 2;

        if (router1.send(rid, static_cast<size_t>(rid_len), zlink::send_flag::sndmore) < 0
            || router1.send(buffer.data(), size) < 0)
            return 2;

        if (use_poll && !wait_for_input(router2, 2000))
            return 2;

        if (router2.recv(rid, sizeof(rid)) < 0 || router2.recv(recv_buf.data(), size) < 0)
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
            if (use_poll && !wait_for_input(router1, 2000)) {
                recv_ok.store(false);
                return;
            }
            if (router1.recv(rid, sizeof(rid)) < 0
                || router1.recv(recv_buf.data(), size) < 0) {
                recv_ok.store(false);
                return;
            }
        }
    });

    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < msg_count; ++i) {
        if (router2.send("ROUTER1", 7, zlink::send_flag::sndmore) < 0
            || router2.send(buffer.data(), size) < 0) {
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

    print_result(use_poll ? "ROUTER_ROUTER_POLL" : "ROUTER_ROUTER",
                 transport,
                 size,
                 throughput,
                 latency);
    return 0;
}

int run_pattern_router_router(const std::string &transport, size_t size)
{
    return run_router_router(transport, size, false);
}
