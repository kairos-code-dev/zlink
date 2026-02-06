#include "../common/bench_common.hpp"
#include <zlink.h>
#include <thread>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <iostream>

#if !defined(_WIN32)
#include <unistd.h>
#else
#include <process.h>
#endif


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
        std::cerr << "recv rid failed: " << zlink_strerror(zlink_errno())
                  << std::endl;
        zlink_msg_close(&rid);
        return false;
    }
    if (!zlink_msg_more(&rid)) {
        std::cerr << "recv rid missing payload" << std::endl;
        zlink_msg_close(&rid);
        return false;
    }
    zlink_msg_t payload;
    zlink_msg_init(&payload);
    if (zlink_msg_recv(&payload, router, 0) < 0) {
        std::cerr << "recv payload failed: "
                  << zlink_strerror(zlink_errno()) << std::endl;
        zlink_msg_close(&rid);
        zlink_msg_close(&payload);
        return false;
    }
    while (zlink_msg_more(&payload)) {
        zlink_msg_t part;
        zlink_msg_init(&part);
        if (zlink_msg_recv(&part, router, 0) < 0) {
            std::cerr << "recv part failed: "
                      << zlink_strerror(zlink_errno()) << std::endl;
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

static bool send_one_gateway(void *gateway, const char *service,
                             size_t msg_size) {
    zlink_msg_t msg;
    zlink_msg_init_size(&msg, msg_size);
    if (msg_size > 0)
        memset(zlink_msg_data(&msg), 'a', msg_size);
    const int rc = zlink_gateway_send(gateway, service, &msg, 1, 0);
    if (rc != 0) {
        std::cerr << "gateway send failed: " << zlink_strerror(zlink_errno())
                  << std::endl;
        zlink_msg_close(&msg);
    }
    return rc == 0;
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
    if (zlink_registry_set_endpoints(registry, reg_pub.c_str(),
                                     reg_router.c_str()) != 0
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

    const char *service_name = "svc2";
    zlink_discovery_subscribe(discovery, service_name);

    void *gateway = zlink_gateway_new_with_routing_id(ctx, discovery, "GW-1");
    if (!gateway) {
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }

    void *provider = zlink_provider_new_with_routing_id(ctx, "PR-1");
    if (!provider) {
        zlink_gateway_destroy(&gateway);
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

    std::string provider_endpoint = make_fixed_endpoint(transport, base_port);
    if (zlink_provider_bind(provider, provider_endpoint.c_str()) != 0
        || zlink_provider_connect_registry(provider, reg_router.c_str()) != 0) {
        print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
        zlink_provider_destroy(&provider);
        zlink_gateway_destroy(&gateway);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }

    void *provider_router = zlink_provider_router(provider);
    if (!provider_router) {
        zlink_provider_destroy(&provider);
        zlink_gateway_destroy(&gateway);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }

    // int hwm = 1000000;
    // zlink_setsockopt(provider_router, ZLINK_SNDHWM, &hwm, sizeof(hwm));
    // zlink_setsockopt(provider_router, ZLINK_RCVHWM, &hwm, sizeof(hwm));

    if (zlink_provider_register(provider, service_name,
                                provider_endpoint.c_str(), 1) != 0) {
        print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
        zlink_provider_destroy(&provider);
        zlink_gateway_destroy(&gateway);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }

    void *recv_router = provider_router;

    void *gateway_router = zlink_gateway_router(gateway);
    if (!gateway_router) {
        print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
        zlink_provider_destroy(&provider);
        zlink_gateway_destroy(&gateway);
        zlink_discovery_destroy(&discovery);
        zlink_registry_destroy(&registry);
        zlink_ctx_term(ctx);
        return;
    }

    // NOTE: direct connect disabled for auto-connect test.
    // if (zlink_connect(gateway_router, provider_endpoint.c_str()) != 0) {
    //     print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
    //     zlink_provider_destroy(&provider);
    //     zlink_gateway_destroy(&gateway);
    //     zlink_discovery_destroy(&discovery);
    //     zlink_registry_destroy(&registry);
    //     zlink_ctx_term(ctx);
    //     return;
    // }

    settle();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const int warmup_count = resolve_bench_count("BENCH_WARMUP_COUNT", 200);
    for (int i = 0; i < warmup_count; ++i) {
        if (!send_one_gateway(gateway, service_name, msg_size)
            || !recv_one_provider_message(recv_router)) {
            print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
            zlink_provider_destroy(&provider);
            zlink_gateway_destroy(&gateway);
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
        if (!send_one_gateway(gateway, service_name, msg_size)
            || !recv_one_provider_message(recv_router)) {
            print_result(lib_name, "GATEWAY", transport, msg_size, 0.0, 0.0);
            zlink_provider_destroy(&provider);
            zlink_gateway_destroy(&gateway);
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
            if (recv_one_provider_message(recv_router))
                ++sent_ok;
        }
    });

    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        if (!send_one_gateway(gateway, service_name, msg_size))
            break;
    }

    receiver.join();
    double throughput = (double)sent_ok / (sw.elapsed_ms() / 1000.0);

    print_result(lib_name, "GATEWAY", transport, msg_size, throughput, latency);

    zlink_provider_destroy(&provider);
    zlink_gateway_destroy(&gateway);
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
