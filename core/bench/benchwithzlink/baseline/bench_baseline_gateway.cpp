#include "../common/bench_common.hpp"
#include <zlink.h>
#include <thread>
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <atomic>
#include <iostream>

#if !defined(_WIN32)
#include <unistd.h>
#else
#include <process.h>
#endif

typedef int (*gateway_set_tls_client_fn)(void *, const char *, const char *, int);
typedef int (*provider_set_tls_server_fn)(void *, const char *, const char *);
typedef void *(*provider_router_fn)(void *);

static const std::string &tls_ca_path() {
    static std::string path =
      write_temp_cert(test_certs::ca_cert_pem, "ca_cert");
    return path;
}

static const std::string &tls_cert_path() {
    static std::string path =
      write_temp_cert(test_certs::server_cert_pem, "server_cert");
    return path;
}

static const std::string &tls_key_path() {
    static std::string path =
      write_temp_cert(test_certs::server_key_pem, "server_key");
    return path;
}

static bool configure_gateway_tls(void *gateway,
                                  const std::string &transport) {
    if (transport != "tls" && transport != "wss")
        return true;
    gateway_set_tls_client_fn fn =
      reinterpret_cast<gateway_set_tls_client_fn>(
        resolve_symbol("zlink_gateway_set_tls_client"));
    if (!fn)
        return false;
    const std::string &ca = tls_ca_path();
    const char *hostname = "localhost";
    const int trust_system = 0;
    return fn(gateway, ca.c_str(), hostname, trust_system) == 0;
}

static bool configure_provider_tls(void *provider,
                                   const std::string &transport) {
    if (transport != "tls" && transport != "wss")
        return true;
    provider_set_tls_server_fn fn =
      reinterpret_cast<provider_set_tls_server_fn>(
        resolve_symbol("zlink_receiver_set_tls_server"));
    if (!fn)
        return false;
    const std::string &cert = tls_cert_path();
    const std::string &key = tls_key_path();
    return fn(provider, cert.c_str(), key.c_str()) == 0;
}

static std::string bind_provider(void *provider, const std::string &transport,
                                 int base_port) {
    for (int i = 0; i < 50; ++i) {
        const int port = base_port + i;
        std::string endpoint = make_fixed_endpoint(transport, port);
        if (zlink_receiver_bind(provider, endpoint.c_str()) == 0)
            return endpoint;
    }
    return std::string();
}

static bool wait_for_discovery(void *discovery, const char *service,
                               int timeout_ms) {
    const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        if (zlink_discovery_service_available(discovery, service) > 0)
            return true;
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
        if (zlink_gateway_connection_count(gateway, service) > 0)
            return true;
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

static bool recv_one_provider_message(void *router) {
    zlink_msg_t rid;
    zlink_msg_init(&rid);
    if (zlink_msg_recv(&rid, router, 0) < 0) {
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
        zlink_msg_close(&rid);
        zlink_msg_close(&payload);
        return false;
    }
    while (zlink_msg_more(&payload)) {
        zlink_msg_t part;
        zlink_msg_init(&part);
        if (zlink_msg_recv(&part, router, 0) < 0) {
            zlink_msg_close(&part);
            zlink_msg_close(&rid);
            zlink_msg_close(&payload);
            return false;
        }
        zlink_msg_close(&part);
    }
    zlink_msg_close(&rid);
    zlink_msg_close(&payload);
    return true;
}

static bool send_one(void *gateway, size_t msg_size) {
    zlink_msg_t msg;
    zlink_msg_init_size(&msg, msg_size);
    if (msg_size > 0)
        memset(zlink_msg_data(&msg), 'a', msg_size);
    const int rc =
      zlink_gateway_send(gateway, "svc", &msg, 1, 0);
    if (rc != 0)
        zlink_msg_close(&msg);
    return rc == 0;
}

void run_gateway(const std::string &transport, size_t msg_size, int msg_count,
                 const std::string &lib_name) {
    if (!transport_available(transport))
        return;

    if (!resolve_symbol("zlink_receiver_router")) {
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
    if (zlink_registry_set_endpoints(registry, reg_pub.c_str(),
                                     reg_router.c_str()) != 0
        || zlink_registry_start(registry) != 0) {
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }

    void *discovery = zlink_discovery_new_typed(ctx, ZLINK_SERVICE_TYPE_GATEWAY_RECEIVER);
    if (!discovery) {
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }
    zlink_discovery_connect_registry(discovery, reg_pub.c_str());
    zlink_discovery_subscribe(discovery, "svc");

    void *provider = zlink_receiver_new(ctx, NULL);
    if (!provider) {
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }

    if (!configure_provider_tls(provider, transport)) {
        print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
        zlink_receiver_destroy(&provider);
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
        zlink_receiver_destroy(&provider);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }

    if (zlink_receiver_connect_registry(provider, reg_router.c_str()) != 0
        || zlink_receiver_register(provider, "svc", endpoint.c_str(), 1) != 0) {
        print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
        zlink_receiver_destroy(&provider);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }

    provider_router_fn router_fn =
      reinterpret_cast<provider_router_fn>(
        resolve_symbol("zlink_receiver_router"));
    void *router = router_fn ? router_fn(provider) : NULL;
    if (!router) {
        print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
        zlink_receiver_destroy(&provider);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }



    void *gateway = zlink_gateway_new(ctx, discovery, NULL);
    if (!gateway) {
        zlink_receiver_destroy(&provider);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }

    if (!wait_for_discovery(discovery, "svc", 8000)) {
        print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
        zlink_gateway_destroy(&gateway);
        zlink_receiver_destroy(&provider);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }

    if (!configure_gateway_tls(gateway, transport)) {
        print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
        zlink_gateway_destroy(&gateway);
        zlink_receiver_destroy(&provider);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }

    if (!wait_for_gateway(gateway, "svc", 8000)) {
        print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
        zlink_gateway_destroy(&gateway);
        zlink_receiver_destroy(&provider);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }

    settle();

    const int warmup_count = resolve_bench_count("BENCH_WARMUP_COUNT", 200);
    for (int i = 0; i < warmup_count; ++i) {
        if (!send_one(gateway, msg_size)
            || !recv_one_provider_message(router)) {
            print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
            zlink_gateway_destroy(&gateway);
            zlink_receiver_destroy(&provider);
            zlink_discovery_destroy(&discovery);
            zlink_registry_destroy(&registry);
            zlink_ctx_term(ctx);
            return;
        }
    }

    stopwatch_t sw;
    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 200);
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        if (!send_one(gateway, msg_size)
            || !recv_one_provider_message(router)) {
            print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
            zlink_gateway_destroy(&gateway);
            zlink_receiver_destroy(&provider);
            zlink_discovery_destroy(&discovery);
            zlink_registry_destroy(&registry);
            zlink_ctx_term(ctx);
            return;
        }
    }
    double latency = (sw.elapsed_ms() * 1000.0) / lat_count;

    std::atomic<int> sent_ok(0);
    std::thread receiver([&]() {
        for (int i = 0; i < msg_count; ++i) {
            if (recv_one_provider_message(router))
                ++sent_ok;
        }
    });

    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        if (!send_one(gateway, msg_size))
            break;
    }

    receiver.join();
    double throughput = (double)sent_ok / (sw.elapsed_ms() / 1000.0);

    print_result(lib_name, "GATEWAY", transport, msg_size, throughput, latency);

    zlink_gateway_destroy(&gateway);
    zlink_receiver_destroy(&provider);
    zlink_discovery_destroy(&discovery);
    zlink_registry_destroy(&registry);
    zlink_ctx_term(ctx);
}

int main(int argc, char **argv) {
    if (argc < 4)
        return 1;
    std::string lib_name = argv[1];
    std::string transport = argv[2];
    size_t size = std::stoul(argv[3]);
    int count = resolve_msg_count(size);
    run_gateway(transport, size, count, lib_name);
    return 0;
}
