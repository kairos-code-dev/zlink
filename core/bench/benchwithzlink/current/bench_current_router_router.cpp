#include "../common/bench_common.hpp"
#include <zlink.h>
#include <thread>
#include <vector>
#include <cstring>
#include <iostream>
#include <atomic>
#include <chrono>
#include <cerrno>

static void *open_monitor(void *socket) {
    return zlink_socket_monitor_open(socket,
                                     ZLINK_EVENT_CONNECTION_READY
                                       | ZLINK_EVENT_DISCONNECTED
                                       | ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL
                                       | ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL
                                       | ZLINK_EVENT_HANDSHAKE_FAILED_AUTH);
}

static void monitor_drain(void *monitor_socket, const char *name) {
    if (!monitor_socket)
        return;
    while (true) {
        zlink_monitor_event_t event;
        const int rc = zlink_monitor_recv(monitor_socket, &event,
                                          ZLINK_DONTWAIT);
        if (rc != 0) {
            if (errno == EAGAIN)
                return;
            return;
        }
        if (event.remote_addr[0] == '\0')
            continue;
        std::cerr << "[router_router] " << name
                  << " monitor event=" << event.event
                  << " endpoint=" << event.remote_addr
                  << std::endl;
    }
}

void run_router_router(const std::string& transport, size_t msg_size, int msg_count, const std::string& lib_name) {
    if (!transport_available(transport)) return;
    void *ctx = zlink_ctx_new();
    void *router1 = zlink_socket(ctx, ZLINK_ROUTER);
    void *router2 = zlink_socket(ctx, ZLINK_ROUTER);

    // Set Routing IDs
    zlink_setsockopt(router1, ZLINK_ROUTING_ID, "ROUTER1", 7);
    zlink_setsockopt(router2, ZLINK_ROUTING_ID, "ROUTER2", 7);

    // Make unroutable messages fail instead of silent drop (for debugging safety)
    int mandatory = 1;
    zlink_setsockopt(router1, ZLINK_ROUTER_MANDATORY, &mandatory, sizeof(mandatory));
    zlink_setsockopt(router2, ZLINK_ROUTER_MANDATORY, &mandatory, sizeof(mandatory));

    // Set very high HWM for benchmarking (default 1000 causes deadlock with IPC)
    int hwm = 100000;
    zlink_setsockopt(router1, ZLINK_SNDHWM, &hwm, sizeof(hwm));
    zlink_setsockopt(router1, ZLINK_RCVHWM, &hwm, sizeof(hwm));
    zlink_setsockopt(router2, ZLINK_SNDHWM, &hwm, sizeof(hwm));
    zlink_setsockopt(router2, ZLINK_RCVHWM, &hwm, sizeof(hwm));

    if (!setup_tls_server(router1, transport) ||
        !setup_tls_client(router2, transport)) {
        zlink_close(router1);
        zlink_close(router2);
        zlink_ctx_term(ctx);
        return;
    }

    std::string endpoint = bind_and_resolve_endpoint(router1, transport, lib_name + "_router_router");
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

    void *monitor1 = NULL;
    void *monitor2 = NULL;
    std::atomic<int> monitor_stop(0);
    std::thread monitor_thread;
    if (bench_debug_enabled()) {
        monitor1 = open_monitor(router1);
        monitor2 = open_monitor(router2);
        monitor_thread = std::thread([&]() {
            while (monitor_stop.load() == 0) {
                monitor_drain(monitor1, "router1");
                monitor_drain(monitor2, "router2");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }

    // --- HANDSHAKE PHASE (Crucial for Router-Router) ---
    // Router drops messages if connection isn't fully established.
    // We loop until we successfully exchange a 'PING' message.
    bool connected = false;
    char buf[16];
    
    // R2 tries to ping R1 until success
    while (!connected) {
        // Try sending PING from R2 to R1
        bench_send_fast(router2, "ROUTER1", 7, ZLINK_SNDMORE | ZLINK_DONTWAIT,
                        "handshake send id");
        int rc = bench_send_fast(router2, "PING", 4, ZLINK_DONTWAIT,
                                 "handshake send ping");

        if (rc == 4) {
            // Check if R1 received it
            int len = bench_recv_fast(router1, buf, 16, ZLINK_DONTWAIT,
                                      "handshake recv id");
            if (len > 0) { // Received ID
                bench_recv_fast(router1, buf, 16, 0,
                                "handshake recv ping"); // Receive "PING"
                connected = true;
            }
        }
        if (!connected) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // Reply PONG to complete handshake
    bench_send_fast(router1, "ROUTER2", 7, ZLINK_SNDMORE,
                    "handshake send id back");
    bench_send_fast(router1, "PONG", 4, 0, "handshake send pong");

    bench_recv_fast(router2, buf, 16, 0, "handshake recv id"); // ID
    bench_recv_fast(router2, buf, 16, 0, "handshake recv pong"); // PONG

    // --- BENCHMARK PHASE ---
    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(msg_size + 256);
    char id[256];
    stopwatch_t sw;

    // Latency Test (1,000 roundtrips)
    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 1000);
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        // R2 -> R1
        bench_send_fast(router2, "ROUTER1", 7, ZLINK_SNDMORE, "lat send id");
        bench_send_fast(router2, buffer.data(), msg_size, 0, "lat send data");

        // R1 Recv
        int id_len =
          bench_recv_fast(router1, id, 256, 0, "lat recv id");
        bench_recv_fast(router1, recv_buf.data(), msg_size, 0, "lat recv data");

        // R1 -> R2 (Reply)
        bench_send_fast(router1, id, id_len, ZLINK_SNDMORE,
                        "lat send id back");
        bench_send_fast(router1, buffer.data(), msg_size, 0,
                        "lat send data back");

        // R2 Recv
        bench_recv_fast(router2, id, 256, 0, "lat recv id back");
        bench_recv_fast(router2, recv_buf.data(), msg_size, 0,
                        "lat recv data back");
    }
    double latency = (sw.elapsed_ms() * 1000.0) / (lat_count * 2);

    // Throughput Test
    std::thread receiver([&]() {
        char id_inner[256];
        for (int i = 0; i < msg_count; ++i) {
            bench_recv_fast(router1, id_inner, 256, 0, "thr recv id");
            bench_recv_fast(router1, recv_buf.data(), msg_size, 0,
                            "thr recv data");
        }
    });

    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        // We use zlink_send_const if available for zero-copy like behavior on send,
        // but here standard send is fine.
        bench_send_fast(router2, "ROUTER1", 7, ZLINK_SNDMORE, "thr send id");
        bench_send_fast(router2, buffer.data(), msg_size, 0, "thr send data");
    }
    receiver.join();
    double throughput = (double)msg_count / (sw.elapsed_ms() / 1000.0);

    print_result(lib_name, "ROUTER_ROUTER", transport, msg_size, throughput, latency);

    monitor_stop.store(1);
    if (monitor_thread.joinable())
        monitor_thread.join();
    if (monitor1)
        zlink_close(monitor1);
    if (monitor2)
        zlink_close(monitor2);

    zlink_close(router1);
    zlink_close(router2);
    zlink_ctx_term(ctx);
}

int main(int argc, char** argv) {
    if (argc < 4) return 1;
    std::string lib_name = argv[1];
    std::string transport = argv[2];
    size_t size = std::stoul(argv[3]);
    int count = resolve_msg_count(size);
    run_router_router(transport, size, count, lib_name);
    return 0;
}
