#include "../common/bench_common_zlink.hpp"
#include <zlink.h>
#include <thread>
#include <vector>
#include <cstring>
#include <iostream>

static bool wait_for_input(zlink_pollitem_t *item, long timeout_ms)
{
    const int rc = zlink_poll(item, 1, timeout_ms);
    if (rc <= 0)
        return false;
    return (item[0].revents & ZLINK_POLLIN) != 0;
}

void run_router_router_poll(const std::string &transport,
                            size_t msg_size,
                            int msg_count,
                            const std::string &lib_name)
{
    if (!transport_available(transport))
        return;
    void *ctx = zlink_ctx_new();
    void *router1 = zlink_socket(ctx, ZLINK_ROUTER);
    void *router2 = zlink_socket(ctx, ZLINK_ROUTER);

    zlink_setsockopt(router1, ZLINK_ROUTING_ID, "ROUTER1", 7);
    zlink_setsockopt(router2, ZLINK_ROUTING_ID, "ROUTER2", 7);

    int mandatory = 1;
    zlink_setsockopt(router1, ZLINK_ROUTER_MANDATORY, &mandatory, sizeof(mandatory));
    zlink_setsockopt(router2, ZLINK_ROUTER_MANDATORY, &mandatory, sizeof(mandatory));

    // Set very high HWM for benchmarking (default 1000 causes deadlock with IPC)
    int hwm = 1000000;
    zlink_setsockopt(router1, ZLINK_SNDHWM, &hwm, sizeof(hwm));
    zlink_setsockopt(router1, ZLINK_RCVHWM, &hwm, sizeof(hwm));
    zlink_setsockopt(router2, ZLINK_SNDHWM, &hwm, sizeof(hwm));
    zlink_setsockopt(router2, ZLINK_RCVHWM, &hwm, sizeof(hwm));

    std::string endpoint =
      bind_and_resolve_endpoint(router1, transport, lib_name + "_router_router_poll");
    if (endpoint.empty()) {
        zlink_close(router1);
        zlink_close(router2);
        zlink_ctx_term(ctx);
        return;
    }
    if (!connect_checked(router2, endpoint)) {
        zlink_close(router1);
        zlink_close(router2);
        zlink_ctx_term(ctx);
        return;
    }
    apply_debug_timeouts(router1, transport);
    apply_debug_timeouts(router2, transport);
    settle();

    zlink_pollitem_t poll_r1[] = {{router1, 0, ZLINK_POLLIN, 0}};
    zlink_pollitem_t poll_r2[] = {{router2, 0, ZLINK_POLLIN, 0}};

    bool connected = false;
    char buf[16];
    const bool debug = bench_debug_enabled();

    if (debug)
        std::cerr << "DEBUG: Starting handshake..." << std::endl;
    int handshake_attempts = 0;
    while (!connected) {
        handshake_attempts++;
        bench_send_fast(router2, "ROUTER1", 7, ZLINK_SNDMORE | ZLINK_DONTWAIT,
                        "handshake send id");
        int rc = bench_send_fast(router2, "PING", 4, ZLINK_DONTWAIT,
                                 "handshake send ping");
        if (debug) {
            std::cerr << "DEBUG: Handshake attempt " << handshake_attempts
                      << ", send rc=" << rc << std::endl;
        }
        if (rc == 4) {
            if (wait_for_input(poll_r1, 0)) {
                if (debug)
                    std::cerr << "DEBUG: poll_r1 has input" << std::endl;
                int len = bench_recv_fast(router1, buf, 16, ZLINK_DONTWAIT,
                                          "handshake recv id");
                if (debug)
                    std::cerr << "DEBUG: recv id len=" << len << std::endl;
                if (len > 0) {
                    bench_recv_fast(router1, buf, 16, ZLINK_DONTWAIT,
                                    "handshake recv ping");
                    connected = true;
                    if (debug)
                        std::cerr << "DEBUG: Handshake connected!" << std::endl;
                }
            }
        }
        if (!connected) {
            if (handshake_attempts > 100) {
                std::cerr << "ERROR: Handshake failed after 100 attempts!" << std::endl;
                zlink_close(router1);
                zlink_close(router2);
                zlink_ctx_term(ctx);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    if (debug)
        std::cerr << "DEBUG: Sending PONG..." << std::endl;
    bench_send_fast(router1, "ROUTER2", 7, ZLINK_SNDMORE,
                    "handshake send id back");
    bench_send_fast(router1, "PONG", 4, 0, "handshake send pong");

    if (debug)
        std::cerr << "DEBUG: Waiting for PONG on router2..." << std::endl;
    if (!wait_for_input(poll_r2, -1)) {
        std::cerr << "ERROR: wait_for_input(poll_r2) failed!" << std::endl;
        zlink_close(router1);
        zlink_close(router2);
        zlink_ctx_term(ctx);
        return;
    }
    if (debug)
        std::cerr << "DEBUG: poll_r2 has input, receiving PONG..." << std::endl;
    bench_recv_fast(router2, buf, 16, 0, "handshake recv id");
    bench_recv_fast(router2, buf, 16, 0, "handshake recv pong");
    if (debug)
        std::cerr << "DEBUG: PONG received! Handshake complete." << std::endl;

    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(msg_size + 256);
    char id[256];
    stopwatch_t sw;

    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 1000);
    if (debug) {
        std::cerr << "DEBUG: Starting latency test (" << lat_count
                  << " iterations)..." << std::endl;
    }
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        if (debug && (i < 3 || i == lat_count - 1)) {
            std::cerr << "DEBUG: Latency iteration " << i << "/" << lat_count
                      << std::endl;
        }
        bench_send_fast(router2, "ROUTER1", 7, ZLINK_SNDMORE, "lat send id");
        bench_send_fast(router2, buffer.data(), msg_size, 0, "lat send data");

        if (!wait_for_input(poll_r1, -1)) {
            std::cerr << "ERROR: wait_for_input(poll_r1) failed at iteration " << i << std::endl;
            return;
        }
        int id_len = bench_recv_fast(router1, id, 256, 0, "lat recv id");
        bench_recv_fast(router1, recv_buf.data(), msg_size, 0,
                        "lat recv data");

        bench_send_fast(router1, id, id_len, ZLINK_SNDMORE,
                        "lat send id back");
        bench_send_fast(router1, buffer.data(), msg_size, 0,
                        "lat send data back");

        if (!wait_for_input(poll_r2, -1)) {
            std::cerr << "ERROR: wait_for_input(poll_r2) failed at iteration " << i << std::endl;
            return;
        }
        bench_recv_fast(router2, id, 256, 0, "lat recv id back");
        bench_recv_fast(router2, recv_buf.data(), msg_size, 0,
                        "lat recv data back");
    }
    if (debug)
        std::cerr << "DEBUG: Latency test complete!" << std::endl;
    double latency = (sw.elapsed_ms() * 1000.0) / (lat_count * 2);

    if (debug) {
        std::cerr << "DEBUG: Starting throughput test (" << msg_count
                  << " messages)..." << std::endl;
    }
    std::thread receiver([&]() {
        char id_inner[256];
        int received = 0;

        if (debug)
            std::cerr << "DEBUG: Receiver thread started" << std::endl;
        while (received < msg_count) {
            if (!wait_for_input(poll_r1, -1))
                return;

            while (received < msg_count) {
                int id_len = bench_recv_fast(router1, id_inner, 256,
                                             ZLINK_DONTWAIT, "thr recv id");
                if (id_len < 0)
                    break;

                bench_recv_fast(router1, recv_buf.data(), msg_size,
                                ZLINK_DONTWAIT, "thr recv data");
                received++;
            }
        }
        if (debug) {
            std::cerr << "DEBUG: Receiver thread complete: " << received
                      << " messages" << std::endl;
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Let receiver start
    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        bench_send_fast(router2, "ROUTER1", 7, ZLINK_SNDMORE, "thr send id");
        bench_send_fast(router2, buffer.data(), msg_size, 0, "thr send data");
    }
    if (debug) {
        std::cerr << "DEBUG: Sent all " << msg_count
                  << " messages, waiting for receiver..." << std::endl;
    }
    receiver.join();
    if (debug)
        std::cerr << "DEBUG: Throughput test complete!" << std::endl;
    double throughput = (double)msg_count / (sw.elapsed_ms() / 1000.0);

    print_result(lib_name, "ROUTER_ROUTER_POLL", transport, msg_size, throughput,
                 latency);

    zlink_close(router1);
    zlink_close(router2);
    zlink_ctx_term(ctx);
}

int main(int argc, char **argv)
{
    if (argc < 4)
        return 1;
    std::string lib_name = argv[1];
    std::string transport = argv[2];
    size_t size = std::stoul(argv[3]);
    int count = resolve_msg_count(size);
    run_router_router_poll(transport, size, count, lib_name);
    return 0;
}
