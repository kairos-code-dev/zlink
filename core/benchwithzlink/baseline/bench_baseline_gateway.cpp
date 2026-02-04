#include "../common/bench_common.hpp"
#include <zlink.h>
#include <thread>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>

#if !defined(_WIN32)
#include <unistd.h>
#else
#include <process.h>
#endif

typedef int (*gateway_set_tls_client_fn)(void *, const char *, const char *, int);
typedef int (*gateway_setsockopt_fn)(void *, int, const void *, size_t);

static void configure_gateway_hwm(void *gateway, int hwm);
typedef int (*provider_set_tls_server_fn)(void *, const char *, const char *);
typedef void *(*provider_router_fn)(void *);

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

static bool recv_one_provider_message(void *router,
                                      std::atomic<int> *recv_fail) {
    zlink_msg_t rid;
    zlink_msg_init(&rid);
    if (zlink_msg_recv(&rid, router, 0) != 0) {
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
    if (zlink_msg_recv(&payload, router, 0) != 0) {
        zlink_msg_close(&rid);
        zlink_msg_close(&payload);
        if (recv_fail)
            ++(*recv_fail);
        return false;
    }
    while (zlink_msg_more(&payload)) {
        zlink_msg_t part;
        zlink_msg_init(&part);
        if (zlink_msg_recv(&part, router, 0) != 0) {
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

static bool send_one(void *gateway, const std::string &service,
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

static bool prime_gateway(void *gateway, void *router, size_t msg_size,
                          std::atomic<int> *send_fail,
                          std::atomic<int> *recv_fail) {
    for (int i = 0; i < 10; ++i) {
        if (send_one(gateway, "svc", msg_size, send_fail)
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

void run_gateway(const std::string &transport, size_t msg_size, int msg_count,
                 const std::string &lib_name) {
    if (!transport_available(transport))
        return;

    if (!resolve_symbol("zlink_provider_router")) {
        print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
        return;
    }

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
    zlink_discovery_subscribe(discovery, "svc");

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
        || zlink_provider_register(provider, "svc", endpoint.c_str(), 1) != 0) {
        print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
        zlink_provider_destroy(&provider);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }

    provider_router_fn router_fn =
      reinterpret_cast<provider_router_fn>(resolve_symbol("zlink_provider_router"));
    void *router = router_fn ? router_fn(provider) : NULL;
    if (!router) {
        print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
        zlink_provider_destroy(&provider);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }
    int hwm = 1000000;
    zlink_setsockopt(router, ZLINK_SNDHWM, &hwm, sizeof(hwm));
    zlink_setsockopt(router, ZLINK_RCVHWM, &hwm, sizeof(hwm));

    void *gateway = zlink_gateway_new(ctx, discovery);
    if (!gateway) {
        zlink_provider_destroy(&provider);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }

    configure_gateway_hwm(gateway, hwm);

    if (!wait_for_discovery(discovery, "svc", 8000)) {
        print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
        zlink_gateway_destroy(&gateway);
        zlink_provider_destroy(&provider);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
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

    if (!wait_for_gateway(gateway, "svc", 8000)) {
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
    if (!prime_gateway(gateway, router, msg_size, &send_fail, &recv_fail)) {
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
        if (!send_one(gateway, "svc", msg_size, &send_fail)
            || !recv_one_provider_message(router, &recv_fail)) {
            ok = false;
            break;
        }
    }
    if (!ok) {
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
        if (!send_one(gateway, "svc", msg_size, &send_fail)
            || !recv_one_provider_message(router, &recv_fail)) {
            ok = false;
            break;
        }
    }
    if (!ok) {
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
        if (!send_one(gateway, "svc", msg_size, &send_fail))
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
static void configure_gateway_hwm(void *gateway, int hwm) {
    gateway_setsockopt_fn fn =
      reinterpret_cast<gateway_setsockopt_fn>(resolve_symbol("zlink_gateway_setsockopt"));
    if (!fn)
        return;
    fn(gateway, ZLINK_SNDHWM, &hwm, sizeof(hwm));
    fn(gateway, ZLINK_RCVHWM, &hwm, sizeof(hwm));
}
