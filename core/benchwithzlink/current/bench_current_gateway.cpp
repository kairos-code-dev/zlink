#include "../common/bench_common.hpp"
#include "../../src/discovery/gateway.hpp"
#include <zlink.h>
#include <thread>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>
#include <iomanip>

#if !defined(_WIN32)
#include <unistd.h>
#else
#include <process.h>
#endif

typedef int (*gateway_set_tls_client_fn)(void *, const char *, const char *, int);
typedef int (*provider_set_tls_server_fn)(void *, const char *, const char *);

static const std::string &tls_ca_path() {
    static std::string path = write_temp_cert(test_certs::ca_cert_pem, "ca_cert");
    return path;
}

static const std::string &tls_cert_path() {
    static std::string path = write_temp_cert(test_certs::server_cert_pem, "server_cert");
    return path;
}

static const std::string &tls_key_path() {
    static std::string path = write_temp_cert(test_certs::server_key_pem, "server_key");
    return path;
}

static bool configure_gateway_tls(void *gateway, const std::string &transport) {
    if (transport != "tls" && transport != "wss")
        return true;
    gateway_set_tls_client_fn fn =
      reinterpret_cast<gateway_set_tls_client_fn>(resolve_symbol("zlink_gateway_set_tls_client"));
    if (!fn)
        return false;
    const std::string &ca = tls_ca_path();
    const char *hostname = "localhost";
    const int trust_system = 0;
    return fn(gateway, ca.c_str(), hostname, trust_system) == 0;
}

static bool configure_provider_tls(void *provider, const std::string &transport) {
    if (transport != "tls" && transport != "wss")
        return true;
    provider_set_tls_server_fn fn =
      reinterpret_cast<provider_set_tls_server_fn>(resolve_symbol("zlink_provider_set_tls_server"));
    if (!fn)
        return false;
    const std::string &cert = tls_cert_path();
    const std::string &key = tls_key_path();
    return fn(provider, cert.c_str(), key.c_str()) == 0;
}

static void log_socket_routing_id(const char *label, void *socket) {
    if (!bench_debug_enabled() || !socket)
        return;
    int type = 0;
    size_t type_size = sizeof(type);
    if (zlink_getsockopt(socket, ZLINK_TYPE, &type, &type_size) == 0) {
        std::cerr << label << " socket_type=" << type << std::endl;
    }
    unsigned char buf[256];
    size_t size = sizeof(buf);
    if (zlink_getsockopt(socket, ZLINK_ROUTING_ID, buf, &size) != 0) {
        std::cerr << label << " routing_id read failed: "
                  << zlink_strerror(zlink_errno()) << std::endl;
        return;
    }
    std::cerr << label << " routing_id_size=" << size << " data=0x";
    for (size_t i = 0; i < size; ++i) {
        std::cerr << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(buf[i]);
    }
    std::cerr << std::dec << std::endl;
}

static std::string bind_provider(void *provider,
                                 const std::string &transport,
                                 int base_port) {
    for (int i = 0; i < 50; ++i) {
        const int port = base_port + i;
        std::string endpoint = make_fixed_endpoint(transport, port);
        if (zlink_provider_bind(provider, endpoint.c_str()) == 0)
            return endpoint;
    }
    return std::string();
}

static int pump_provider(void *router, int timeout_ms) {
    static int logged = 0;
    zlink_pollitem_t item;
    item.socket = router;
    item.fd = 0;
    item.events = ZLINK_POLLIN;
    item.revents = 0;
    const int prc = zlink_poll(&item, 1, timeout_ms);
    if (prc <= 0 || !(item.revents & ZLINK_POLLIN))
        return 0;

    int processed = 0;
    while (true) {
        zlink_msg_t rid;
        zlink_msg_init(&rid);
        const int rid_rc = zlink_msg_recv(&rid, router, ZLINK_DONTWAIT);
        if (rid_rc < 0) {
            zlink_msg_close(&rid);
            if (zlink_errno() == EAGAIN)
                break;
            if (bench_debug_enabled()) {
                std::cerr << "pump_provider recv rid failed: "
                          << zlink_strerror(zlink_errno()) << std::endl;
            }
            return -1;
        }
        if (!zlink_msg_more(&rid)) {
            if (bench_debug_enabled()) {
                std::cerr << "pump_provider rid missing more size="
                          << zlink_msg_size(&rid) << std::endl;
            }
            const bool empty = zlink_msg_size(&rid) == 0;
            zlink_msg_close(&rid);
            if (empty)
                continue;
            return -1;
        }
        if (bench_debug_enabled() && logged < 1) {
            std::cerr << "pump_provider rid_size=" << zlink_msg_size(&rid)
                      << std::endl;
            ++logged;
        }
        zlink_msg_t payload;
        zlink_msg_init(&payload);
        if (zlink_msg_recv(&payload, router, 0) < 0) {
            if (bench_debug_enabled()) {
                std::cerr << "pump_provider recv payload failed: "
                          << zlink_strerror(zlink_errno()) << std::endl;
            }
            zlink_msg_close(&rid);
            zlink_msg_close(&payload);
            return -1;
        }

        std::vector<zlink_msg_t> parts;
        while (zlink_msg_more(&payload)) {
            zlink_msg_t part;
            zlink_msg_init(&part);
            if (zlink_msg_recv(&part, router, 0) < 0) {
                zlink_msg_close(&part);
                break;
            }
            parts.push_back(part);
            if (!zlink_msg_more(&part))
                break;
        }

        if (zlink_msg_send(&rid, router, ZLINK_SNDMORE) < 0) {
            if (bench_debug_enabled()) {
                std::cerr << "pump_provider send rid failed: "
                          << zlink_strerror(zlink_errno()) << std::endl;
            }
            zlink_msg_close(&rid);
            zlink_msg_close(&payload);
            for (size_t i = 0; i < parts.size(); ++i)
                zlink_msg_close(&parts[i]);
            return -1;
        }
        int flags = parts.empty() ? 0 : ZLINK_SNDMORE;
        if (zlink_msg_send(&payload, router, flags) < 0) {
            if (bench_debug_enabled()) {
                std::cerr << "pump_provider send payload failed: "
                          << zlink_strerror(zlink_errno()) << std::endl;
            }
            zlink_msg_close(&payload);
            for (size_t i = 0; i < parts.size(); ++i)
                zlink_msg_close(&parts[i]);
            return -1;
        }
        for (size_t i = 0; i < parts.size(); ++i) {
            flags = (i + 1 < parts.size()) ? ZLINK_SNDMORE : 0;
            if (zlink_msg_send(&parts[i], router, flags) < 0) {
                if (bench_debug_enabled()) {
                    std::cerr << "pump_provider send part failed: "
                              << zlink_strerror(zlink_errno()) << std::endl;
                }
                break;
            }
        }

        zlink_msg_close(&rid);
        zlink_msg_close(&payload);
        for (size_t i = 0; i < parts.size(); ++i)
            zlink_msg_close(&parts[i]);
        processed++;
    }

    return processed;
}

static bool recv_one_provider_message(void *router,
                                      std::atomic<int> *recv_fail) {
    zlink_msg_t rid;
    zlink_msg_init(&rid);
    if (zlink_msg_recv(&rid, router, 0) < 0) {
        zlink_msg_close(&rid);
        if (recv_fail)
            ++(*recv_fail);
        return false;
    }
    if (!zlink_msg_more(&rid)) {
        zlink_msg_close(&rid);
        if (recv_fail)
            ++(*recv_fail);
        return false;
    }
    zlink_msg_t payload;
    zlink_msg_init(&payload);
    if (zlink_msg_recv(&payload, router, 0) < 0) {
        zlink_msg_close(&rid);
        zlink_msg_close(&payload);
        if (recv_fail)
            ++(*recv_fail);
        return false;
    }
    while (zlink_msg_more(&payload)) {
        zlink_msg_t part;
        zlink_msg_init(&part);
        if (zlink_msg_recv(&part, router, 0) < 0) {
            zlink_msg_close(&part);
            if (recv_fail)
                ++(*recv_fail);
            break;
        }
        zlink_msg_close(&part);
        if (!zlink_msg_more(&part))
            break;
    }
    zlink_msg_close(&rid);
    zlink_msg_close(&payload);
    return true;
}

static bool send_one_gateway(void *gateway, const std::string &service,
                             size_t msg_size, std::atomic<int> *send_fail) {
    zlink_msg_t msg;
    zlink_msg_init_size(&msg, msg_size);
    if (msg_size > 0)
        memset(zlink_msg_data(&msg), 'a', msg_size);
    const int rc =
      zlink_gateway_send(gateway, service.c_str(), &msg, 1, 0);
    if (rc != 0)
        zlink_msg_close(&msg);
    if (rc != 0 && send_fail)
        ++(*send_fail);
    return rc == 0;
}

static bool send_one_direct(void *router,
                            const zlink_routing_id_t &rid,
                            size_t msg_size) {
    if (!router || rid.size == 0)
        return false;
    zlink_pollitem_t out_item;
    out_item.socket = router;
    out_item.fd = 0;
    out_item.events = ZLINK_POLLOUT;
    out_item.revents = 0;
    if (zlink_poll(&out_item, 1, 2000) <= 0
        || !(out_item.revents & ZLINK_POLLOUT)) {
        if (bench_debug_enabled()) {
            std::cerr << "direct send pollout timeout" << std::endl;
        }
        return false;
    }
    zlink_msg_t rid_msg;
    zlink_msg_init_size(&rid_msg, rid.size);
    memcpy(zlink_msg_data(&rid_msg), rid.data, rid.size);

    zlink_msg_t msg;
    zlink_msg_init_size(&msg, msg_size);
    if (msg_size > 0)
        memset(zlink_msg_data(&msg), 'a', msg_size);

    const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
    while (true) {
        const int rc1 = zlink_msg_send(&rid_msg, router, ZLINK_SNDMORE);
        if (rc1 == 0)
            break;
        if (zlink_errno() != EAGAIN
            || std::chrono::steady_clock::now() >= deadline) {
            if (bench_debug_enabled()) {
                std::cerr << "gateway direct send failed: "
                          << zlink_strerror(zlink_errno()) << std::endl;
            }
            zlink_msg_close(&rid_msg);
            zlink_msg_close(&msg);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    while (true) {
        const int rc2 = zlink_msg_send(&msg, router, 0);
        if (rc2 == 0)
            break;
        if (zlink_errno() != EAGAIN
            || std::chrono::steady_clock::now() >= deadline) {
            if (bench_debug_enabled()) {
                std::cerr << "gateway direct send failed: "
                          << zlink_strerror(zlink_errno()) << std::endl;
            }
            zlink_msg_close(&msg);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

static bool prime_gateway_sendonly(void *gateway, void *router,
                                   size_t msg_size,
                                   std::atomic<int> *send_fail,
                                   std::atomic<int> *recv_fail) {
    for (int i = 0; i < 10; ++i) {
        if (send_one_gateway(gateway, "svc", msg_size, send_fail)
            && recv_one_provider_message(router, recv_fail)) {
            return true;
        }
    }
    return false;
}

static bool wait_for_discovery(void *discovery, const char *service,
                               int timeout_ms) {
    const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        const int available = zlink_discovery_service_available(discovery, service);
        if (available > 0)
            return true;
        if (bench_debug_enabled()) {
            const int count = zlink_discovery_provider_count(discovery, service);
            std::cerr << "discovery waiting: available=" << available
                      << " providers=" << count << std::endl;
        }
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

static bool wait_for_gateway(void *gateway, const char *service,
                             int timeout_ms) {
    const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        const int count = zlink_gateway_connection_count(gateway, service);
        if (count > 0)
            return true;
        if (bench_debug_enabled()) {
            std::cerr << "gateway waiting: connections=" << count << std::endl;
        }
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

static void log_provider_peers(void *router, const char *label) {
    if (!bench_debug_enabled() || !router)
        return;
    const int peer_count = zlink_socket_peer_count(router);
    std::cerr << label << " peer_count=" << peer_count << std::endl;
    if (peer_count <= 0)
        return;
    size_t count = static_cast<size_t>(peer_count);
    std::vector<zlink_peer_info_t> peers;
    peers.resize(count);
    if (zlink_socket_peers(router, &peers[0], &count) != 0)
        return;
    for (size_t i = 0; i < count; ++i) {
        const zlink_peer_info_t &info = peers[i];
        std::cerr << label << " peer[" << i << "] remote=" << info.remote_addr
                  << " rid_size=" << static_cast<int>(info.routing_id.size)
                  << " rid=0x";
        for (size_t j = 0; j < info.routing_id.size; ++j) {
            std::cerr << std::hex << std::setw(2) << std::setfill('0')
                      << static_cast<int>(info.routing_id.data[j]);
        }
        std::cerr << std::dec << " sent=" << info.msgs_sent
                  << " recv=" << info.msgs_received << std::endl;
    }
}

void run_gateway(const std::string &transport, size_t msg_size, int msg_count,
                 const std::string &lib_name) {
    if (!transport_available(transport))
        return;

    if ((transport == "tls" || transport == "wss")
        && !resolve_symbol("zlink_gateway_set_tls_client")) {
        print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
        return;
    }

    void *ctx = zlink_ctx_new();
    if (!ctx)
        return;

    std::string suffix = lib_name + "_gw_" + transport;
#if !defined(_WIN32)
    suffix += "_" + std::to_string(getpid());
#else
    suffix += "_" + std::to_string(_getpid());
#endif

    std::string reg_pub = "inproc://gw_pub_" + suffix;
    std::string reg_router = "inproc://gw_router_" + suffix;

    void *registry = zlink_registry_new(ctx);
    if (!registry) {
        zlink_ctx_term(ctx);
        return;
    }
    if (zlink_registry_set_endpoints(registry, reg_pub.c_str(), reg_router.c_str()) != 0
        || zlink_registry_start(registry) != 0) {
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }

    void *discovery = zlink_discovery_new(ctx);
    if (!discovery) {
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }
    zlink_discovery_connect_registry(discovery, reg_pub.c_str());
    const char *service_name = "svc";
    zlink_discovery_subscribe(discovery, service_name);

    void *provider = zlink_provider_new(ctx);
    if (!provider) {
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }

    if (!configure_provider_tls(provider, transport)) {
        print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
        zlink_provider_destroy(&provider);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }

    int base_port = 30000;
#if !defined(_WIN32)
    base_port += (getpid() % 2000);
#else
    base_port += (_getpid() % 2000);
#endif
    std::string endpoint = bind_provider(provider, transport, base_port);
    if (endpoint.empty()) {
        print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
        zlink_provider_destroy(&provider);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }

    if (zlink_provider_connect_registry(provider, reg_router.c_str()) != 0
        || zlink_provider_register(provider, service_name, endpoint.c_str(), 1) != 0) {
        print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
        zlink_provider_destroy(&provider);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }

    void *router = zlink_provider_router(provider);
    if (!router) {
        print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
        zlink_provider_destroy(&provider);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }
    int probe = 1;
    zlink_setsockopt(router, ZLINK_PROBE_ROUTER, &probe, sizeof(probe));
    int hwm = 1000000;
    zlink_setsockopt(router, ZLINK_SNDHWM, &hwm, sizeof(hwm));
    zlink_setsockopt(router, ZLINK_RCVHWM, &hwm, sizeof(hwm));
    log_socket_routing_id("provider router", router);

    void *gateway = zlink_gateway_new(ctx, discovery);
    if (!gateway) {
        zlink_provider_destroy(&provider);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }

    if (!wait_for_discovery(discovery, service_name, 8000)) {
        print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
        zlink_gateway_destroy(&gateway);
        zlink_provider_destroy(&provider);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }
    if (bench_debug_enabled()) {
        log_provider_peers(router, "provider router");
    }
    if (bench_debug_enabled()) {
        size_t count = 0;
        zlink_discovery_get_providers(discovery, "svc", NULL, &count);
        std::vector<zlink_provider_info_t> providers;
        providers.resize(count);
        if (count > 0
            && zlink_discovery_get_providers(discovery, "svc",
                                             &providers[0], &count) == 0) {
            std::cerr << "discovery providers=" << count
                      << " routing_id_size="
                      << static_cast<int>(providers[0].routing_id.size)
                      << std::endl;
            std::cerr << "discovery routing_id data=0x";
            for (size_t i = 0; i < providers[0].routing_id.size; ++i) {
                std::cerr << std::hex << std::setw(2) << std::setfill('0')
                          << static_cast<int>(providers[0].routing_id.data[i]);
            }
            std::cerr << std::dec << std::endl;
        }
    }

    if (!configure_gateway_tls(gateway, transport)) {
        print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
        zlink_gateway_destroy(&gateway);
        zlink_provider_destroy(&provider);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }

    zlink_gateway_setsockopt(gateway, ZLINK_SNDHWM, &hwm, sizeof(hwm));
    zlink_gateway_setsockopt(gateway, ZLINK_RCVHWM, &hwm, sizeof(hwm));

    if (!wait_for_gateway(gateway, service_name, 8000)) {
        if (bench_debug_enabled()) {
            std::cerr << "gateway connect timeout" << std::endl;
        }
        print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
        zlink_gateway_destroy(&gateway);
        zlink_provider_destroy(&provider);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }

    settle();
    std::atomic<int> send_fail(0);
    std::atomic<int> recv_fail(0);
    if (!prime_gateway_sendonly(gateway, router, msg_size,
                                &send_fail, &recv_fail)) {
        if (bench_debug_enabled()) {
            log_provider_peers(router, "provider router after prime");
        }
        print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
        zlink_gateway_destroy(&gateway);
        zlink_provider_destroy(&provider);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }
    const int warmup_count = resolve_bench_count("BENCH_WARMUP_COUNT", 200);
    bool ok = true;
    for (int i = 0; i < warmup_count; ++i) {
        if (!send_one_gateway(gateway, service_name, msg_size, &send_fail)
            || !recv_one_provider_message(router, &recv_fail)) {
            ok = false;
            break;
        }
    }
    if (!ok) {
        if (bench_debug_enabled()) {
            std::cerr << "warmup failed" << std::endl;
        }
        print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
        zlink_gateway_destroy(&gateway);
        zlink_provider_destroy(&provider);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }

    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 200);
    stopwatch_t sw;
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        if (!send_one_gateway(gateway, service_name, msg_size, &send_fail)
            || !recv_one_provider_message(router, &recv_fail)) {
            ok = false;
            break;
        }
    }
    if (!ok) {
        if (bench_debug_enabled()) {
            std::cerr << "latency loop failed" << std::endl;
        }
        print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
        zlink_gateway_destroy(&gateway);
        zlink_provider_destroy(&provider);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }
    double latency = (sw.elapsed_ms() * 1000.0) / lat_count;

    std::atomic<int> recv_count(0);
    std::thread receiver([&]() {
        for (int i = 0; i < msg_count; ++i) {
            if (recv_one_provider_message(router, &recv_fail))
                ++recv_count;
        }
    });
    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        if (!send_one_gateway(gateway, service_name, msg_size, &send_fail))
            break;
    }
    const uint64_t deadline_ms = sw.elapsed_ms () + 5000;
    while (true) {
        if (recv_count.load() >= msg_count)
            break;
        if (sw.elapsed_ms() >= deadline_ms)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    receiver.join();
    if (msg_count == 0) {
        if (bench_debug_enabled()) {
            std::cerr << "throughput msg_count=0" << std::endl;
        }
        print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, latency);
        zlink_gateway_destroy(&gateway);
        zlink_provider_destroy(&provider);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }
    double throughput = (double)msg_count / (sw.elapsed_ms() / 1000.0);

    if (bench_debug_enabled()) {
        std::cerr << "[gwbench] send_fail=" << send_fail.load()
                  << " recv_fail=" << recv_fail.load()
                  << " recv_count=" << recv_count.load()
                  << " msg_count=" << msg_count << std::endl;
    }

    print_result(lib_name, "GATEWAY", transport, msg_size, throughput, latency);

    zlink_gateway_destroy(&gateway);
    zlink_provider_destroy(&provider);
    zlink_discovery_destroy(&discovery);
    zlink_registry_destroy(&registry);
    zlink_ctx_term(ctx);
}

int main(int argc, char **argv) {
    if (argc < 4)
        return 1;
#if !defined(_WIN32)
    if (!std::getenv("BENCH_GATEWAY_SINGLE_THREAD"))
        setenv("ZLINK_GATEWAY_SINGLE_THREAD", "1", 1);
#else
    if (!std::getenv("BENCH_GATEWAY_SINGLE_THREAD"))
        _putenv_s("ZLINK_GATEWAY_SINGLE_THREAD", "1");
#endif
    std::string lib_name = argv[1];
    std::string transport = argv[2];
    size_t size = std::stoul(argv[3]);
    int count = resolve_msg_count(size);
    run_gateway(transport, size, count, lib_name);
    return 0;
}
