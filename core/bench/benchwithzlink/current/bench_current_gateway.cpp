#include "../common/bench_common.hpp"
#include <zlink.h>
#include <thread>
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <atomic>
#include <iostream>
#include <cerrno>

#if !defined(_WIN32)
#include <unistd.h>
#else
#include <process.h>
#endif

static void *open_monitor(void *socket) {
    return zlink_socket_monitor_open(
      socket,
      ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED
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
        std::cerr << "[monitor] " << name << " event=" << event.event
                  << " endpoint=" << event.remote_addr << std::endl;
    }
}

static std::string bind_router_socket(void *router,
                                      const std::string &transport,
                                      int base_port) {
    for (int i = 0; i < 50; ++i) {
        const int port = base_port + i;
        std::string endpoint = make_fixed_endpoint(transport, port);
        if (zlink_bind(router, endpoint.c_str()) == 0)
            return endpoint;
    }
    return std::string();
}

static bool recv_one_provider_message(void *router) {
    zlink_msg_t rid;
    zlink_msg_init(&rid);
    if (zlink_msg_recv(&rid, router, 0) < 0) {
        if (bench_debug_enabled())
            std::cerr << "gateway recv rid failed: "
                      << zlink_strerror(zlink_errno()) << std::endl;
        zlink_msg_close(&rid);
        return false;
    }
    if (!zlink_msg_more(&rid)) {
        zlink_msg_close(&rid);
        return false;
    }
    zlink_msg_t payload;
    zlink_msg_init(&payload);
    if (zlink_msg_recv(&payload, router, 0) < 0) {
        if (bench_debug_enabled())
            std::cerr << "gateway recv payload failed: "
                      << zlink_strerror(zlink_errno()) << std::endl;
        zlink_msg_close(&rid);
        zlink_msg_close(&payload);
        return false;
    }
    zlink_msg_close(&rid);
    zlink_msg_close(&payload);
    return true;
}

static bool recv_id_payload(void *router, std::vector<char> &id_buf,
                            size_t &id_len) {
    zlink_msg_t rid;
    zlink_msg_init(&rid);
    if (zlink_msg_recv(&rid, router, 0) < 0) {
        if (bench_debug_enabled())
            std::cerr << "gateway recv rid failed: "
                      << zlink_strerror(zlink_errno()) << std::endl;
        zlink_msg_close(&rid);
        return false;
    }
    if (!zlink_msg_more(&rid)) {
        zlink_msg_close(&rid);
        return false;
    }
    id_len = zlink_msg_size(&rid);
    if (id_len > id_buf.size())
        id_len = id_buf.size();
    if (id_len > 0)
        memcpy(id_buf.data(), zlink_msg_data(&rid), id_len);

    zlink_msg_t payload;
    zlink_msg_init(&payload);
    if (zlink_msg_recv(&payload, router, 0) < 0) {
        if (bench_debug_enabled())
            std::cerr << "gateway recv payload failed: "
                      << zlink_strerror(zlink_errno()) << std::endl;
        zlink_msg_close(&rid);
        zlink_msg_close(&payload);
        return false;
    }
    zlink_msg_close(&rid);
    zlink_msg_close(&payload);
    return true;
}

static bool send_one(void *router,
                     const char *target_id,
                     size_t target_id_len,
                     size_t msg_size) {
    zlink_msg_t rid;
    zlink_msg_init_size(&rid, target_id_len);
    if (target_id_len > 0)
        memcpy(zlink_msg_data(&rid), target_id, target_id_len);
    if (zlink_msg_send(&rid, router, ZLINK_SNDMORE) < 0) {
        if (bench_debug_enabled()) {
            std::cerr << "gateway send rid failed: "
                      << zlink_strerror(zlink_errno()) << std::endl;
        }
        zlink_msg_close(&rid);
        return false;
    }
    zlink_msg_close(&rid);

    zlink_msg_t payload;
    zlink_msg_init_size(&payload, msg_size);
    if (msg_size > 0)
        memset(zlink_msg_data(&payload), 'a', msg_size);
    if (zlink_msg_send(&payload, router, 0) < 0) {
        if (bench_debug_enabled()) {
            std::cerr << "gateway send payload failed: "
                      << zlink_strerror(zlink_errno()) << std::endl;
        }
        zlink_msg_close(&payload);
        return false;
    }
    zlink_msg_close(&payload);
    return true;
}

static bool send_payload(void *router,
                         const char *target_id,
                         size_t target_id_len,
                         const void *data,
                         size_t data_len) {
    zlink_msg_t rid;
    zlink_msg_init_size(&rid, target_id_len);
    if (target_id_len > 0)
        memcpy(zlink_msg_data(&rid), target_id, target_id_len);
    if (zlink_msg_send(&rid, router, ZLINK_SNDMORE) < 0) {
        if (bench_debug_enabled()) {
            std::cerr << "gateway send rid failed: "
                      << zlink_strerror(zlink_errno()) << std::endl;
        }
        zlink_msg_close(&rid);
        return false;
    }
    zlink_msg_close(&rid);

    zlink_msg_t payload;
    zlink_msg_init_size(&payload, data_len);
    if (data_len > 0)
        memcpy(zlink_msg_data(&payload), data, data_len);
    if (zlink_msg_send(&payload, router, 0) < 0) {
        if (bench_debug_enabled()) {
            std::cerr << "gateway send payload failed: "
                      << zlink_strerror(zlink_errno()) << std::endl;
        }
        zlink_msg_close(&payload);
        return false;
    }
    zlink_msg_close(&payload);
    return true;
}

void run_gateway(const std::string &transport, size_t msg_size, int msg_count,
                 const std::string &lib_name) {
    if (!transport_available(transport))
        return;

    void *ctx = zlink_ctx_new();
    if (!ctx)
        return;

    void *router1 = zlink_socket(ctx, ZLINK_ROUTER);
    void *router2 = zlink_socket(ctx, ZLINK_ROUTER);
    if (!router1 || !router2) {
        if (router1)
            zlink_close(router1);
        if (router2)
            zlink_close(router2);
        zlink_ctx_term(ctx);
        return;
    }

    zlink_setsockopt(router1, ZLINK_ROUTING_ID, "SERVER", 6);
    zlink_setsockopt(router2, ZLINK_ROUTING_ID, "CLIENT", 6);

    int mandatory = 1;
    zlink_setsockopt(router1, ZLINK_ROUTER_MANDATORY, &mandatory,
                     sizeof(mandatory));
    zlink_setsockopt(router2, ZLINK_ROUTER_MANDATORY, &mandatory,
                     sizeof(mandatory));

    int hwm = 1000000;
    zlink_setsockopt(router1, ZLINK_SNDHWM, &hwm, sizeof(hwm));
    zlink_setsockopt(router1, ZLINK_RCVHWM, &hwm, sizeof(hwm));
    zlink_setsockopt(router2, ZLINK_SNDHWM, &hwm, sizeof(hwm));
    zlink_setsockopt(router2, ZLINK_RCVHWM, &hwm, sizeof(hwm));

    if (!setup_tls_server(router1, transport)
        || !setup_tls_client(router2, transport)) {
        zlink_close(router1);자
        zlink_close(router2);
        zlink_ctx_term(ctx);
        return;
    }

    int base_port = 30000;
#if !defined(_WIN32)
    base_port += (getpid() % 2000);
#else
    base_port += (_getpid() % 2000);
#endif
    std::string endpoint = bind_router_socket(router1, transport, base_port);
    if (endpoint.empty()) {
        zlink_close(router1);
        zlink_close(router2);
        zlink_ctx_term(ctx);
        return;
    }
    if (zlink_connect(router2, endpoint.c_str()) != 0) {
        zlink_close(router1);
        zlink_close(router2);
        zlink_ctx_term(ctx);
        return;
    }

    const int io_timeout_ms = 5000;
    zlink_setsockopt(router1, ZLINK_SNDTIMEO, &io_timeout_ms,
                     sizeof(io_timeout_ms));
    zlink_setsockopt(router1, ZLINK_RCVTIMEO, &io_timeout_ms,
                     sizeof(io_timeout_ms));
    zlink_setsockopt(router2, ZLINK_SNDTIMEO, &io_timeout_ms,
                     sizeof(io_timeout_ms));
    zlink_setsockopt(router2, ZLINK_RCVTIMEO, &io_timeout_ms,
                     sizeof(io_timeout_ms));

    settle();
    if (bench_debug_enabled())
        std::cerr << "gateway: settle done" << std::endl;

    void *monitor1 = NULL;
    void *monitor2 = NULL;
    std::atomic<int> monitor_stop(0);
    std::thread monitor_thread;
    if (bench_debug_enabled()) {
        monitor1 = open_monitor(router1);
        monitor2 = open_monitor(router2);
        monitor_thread = std::thread([&]() {
            while (monitor_stop.load() == 0) {
                monitor_drain(monitor1, "server_router");
                monitor_drain(monitor2, "client_router");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }

    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(msg_size + 256);
    stopwatch_t sw;

    const int warmup_count = 1;
    for (int i = 0; i < warmup_count; ++i) {
        if (bench_debug_enabled() && i == 0)
            std::cerr << "gateway: warmup start" << std::endl;
        bench_send_fast(router2, "SERVER", 6, ZLINK_SNDMORE, "warmup send id");
        bench_send_fast(router2, buffer.data(), msg_size, 0,
                        "warmup send data");
        if (!recv_one_provider_message(router1)) {
            if (bench_debug_enabled())
                std::cerr << "warmup recv failed" << std::endl;
            print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
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
            return;
        }
    }

    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 200);
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        if (bench_debug_enabled() && i == 0)
            std::cerr << "gateway: latency start" << std::endl;
        if (!send_one(router2, "SERVER", 6, msg_size)
            || !recv_one_provider_message(router1)) {
            print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
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
            return;
        }
    }
    double latency = (sw.elapsed_ms() * 1000.0) / lat_count;

    std::atomic<int> sent_ok(0);
    std::atomic<int> recv_fail(0);
    std::thread receiver([&]() {
        if (bench_debug_enabled())
            std::cerr << "gateway: throughput recv start" << std::endl;
        for (int i = 0; i < msg_count; ++i) {
            if (recv_one_provider_message(router1)) {
                ++sent_ok;
            } else {
                if (bench_debug_enabled())
                    std::cerr << "gateway recv failed at " << i << std::endl;
                ++recv_fail;
                break;
            }
        }
    });

    sw.start();
    int send_fail = 0;
    for (int i = 0; i < msg_count; ++i) {
        if (!send_one(router2, "SERVER", 6, msg_size)) {
            if (bench_debug_enabled())
                std::cerr << "gateway send failed at " << i << std::endl;
            send_fail = 1;
            break;
        }
    }
    receiver.join();
    const double elapsed_ms = sw.elapsed_ms();
    if (bench_debug_enabled()) {
        std::cerr << "gateway throughput sent_ok=" << sent_ok.load()
                  << " elapsed_ms=" << elapsed_ms << std::endl;
    }
    if (bench_debug_enabled() && (send_fail || recv_fail.load() > 0)) {
        std::cerr << "gateway send_fail=" << send_fail
                  << " recv_fail=" << recv_fail.load()
                  << " sent_ok=" << sent_ok.load()
                  << " msg_count=" << msg_count << std::endl;
    }
    double throughput = 0.0;
    if (elapsed_ms <= 0.0) {
        std::cerr << "[gwbench] invalid elapsed_ms=" << elapsed_ms
                  << " sent_ok=" << sent_ok.load()
                  << " send_fail=" << send_fail
                  << " recv_fail=" << recv_fail.load() << std::endl;
    } else {
        throughput = (double)sent_ok / (elapsed_ms / 1000.0);
        if (throughput < 0.0) {
            std::cerr << "[gwbench] negative throughput=" << throughput
                      << " elapsed_ms=" << elapsed_ms
                      << " sent_ok=" << sent_ok.load()
                      << " send_fail=" << send_fail
                      << " recv_fail=" << recv_fail.load() << std::endl;
        }
    }

    print_result(lib_name, "GATEWAY", transport, msg_size, throughput, latency);

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

int main(int argc, char **argv) {
    if (argc < 4)
        return 1;
#if !defined(_WIN32)
    setenv("ZLINK_GATEWAY_SINGLE_THREAD", "1", 1);
#else
    _putenv_s("ZLINK_GATEWAY_SINGLE_THREAD", "1");
#endif
    std::string lib_name = argv[1];
    std::string transport = argv[2];
    size_t size = std::stoul(argv[3]);
    int count = resolve_msg_count(size);
    run_gateway(transport, size, count, lib_name);
    return 0;
}
