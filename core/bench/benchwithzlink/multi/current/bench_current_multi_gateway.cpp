#include "../common/bench_common.hpp"
#include "../common/bench_common_multi.hpp"
#include <zlink.h>
#include <atomic>
#include <cstring>
#include <vector>
#include <thread>

#if !defined(_WIN32)
#include <unistd.h>
#else
#include <process.h>
#endif

typedef int (*gateway_set_tls_client_fn)(void *, const char *, const char *, int);
typedef int (*provider_set_tls_server_fn)(void *, const char *, const char *);

static const std::string &tls_ca_path()
{
    static std::string path = write_temp_cert(test_certs::ca_cert_pem, "gw_ca_cert");
    return path;
}

static const std::string &tls_cert_path()
{
    static std::string path = write_temp_cert(test_certs::server_cert_pem, "gw_server_cert");
    return path;
}

static const std::string &tls_key_path()
{
    static std::string path = write_temp_cert(test_certs::server_key_pem, "gw_server_key");
    return path;
}

static bool configure_gateway_tls(void *gateway, const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    gateway_set_tls_client_fn fn =
      reinterpret_cast<gateway_set_tls_client_fn>(
        resolve_symbol("zlink_gateway_set_tls_client"));
    if (!fn)
        return false;

    const std::string &ca = tls_ca_path();
    return fn(gateway, ca.c_str(), "localhost", 0) == 0;
}

static bool configure_provider_tls(void *provider, const std::string &transport)
{
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

static std::string bind_provider(void *provider,
                               const std::string &transport,
                               int base_port)
{
    for (int i = 0; i < 50; ++i) {
        const int port = base_port + i;
        std::string endpoint = make_fixed_endpoint(transport, port);
        if (zlink_receiver_bind(provider, endpoint.c_str()) == 0)
            return endpoint;
    }
    return std::string();
}

static bool wait_for_discovery(void *discovery,
                               const char *service,
                               int timeout_ms)
{
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

static bool wait_for_gateway(void *gateway, const char *service, int timeout_ms)
{
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

static bool recv_one_provider_message(void *router)
{
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

static bool recv_one_provider_message_nowait(void *router)
{
    zlink_msg_t rid;
    zlink_msg_init(&rid);
    if (zlink_msg_recv(&rid, router, ZLINK_DONTWAIT) < 0) {
        zlink_msg_close(&rid);
        if (zlink_errno() == EAGAIN)
            return false;
        return false;
    }
    if (!zlink_msg_more(&rid)) {
        zlink_msg_close(&rid);
        return false;
    }

    zlink_msg_t payload;
    zlink_msg_init(&payload);
    if (zlink_msg_recv(&payload, router, ZLINK_DONTWAIT) < 0) {
        zlink_msg_close(&rid);
        zlink_msg_close(&payload);
        return false;
    }

    while (zlink_msg_more(&payload)) {
        zlink_msg_t part;
        zlink_msg_init(&part);
        if (zlink_msg_recv(&part, router, ZLINK_DONTWAIT) < 0) {
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

static bool recv_one_provider_any(const std::vector<void *> &routers)
{
    for (void *router : routers) {
        if (recv_one_provider_message_nowait(router))
            return true;
    }
    return false;
}

static bool send_one_gateway(void *gateway,
                            const char *service,
                            size_t msg_size)
{
    zlink_msg_t msg;
    zlink_msg_init_size(&msg, msg_size);
    if (msg_size > 0)
        memset(zlink_msg_data(&msg), 'a', msg_size);

    const int rc = zlink_gateway_send(gateway, service, &msg, 1, 0);
    if (rc != 0)
        zlink_msg_close(&msg);
    return rc == 0;
}

void run_multi_gateway(const std::string &transport,
                      size_t msg_size,
                      int /*msg_count*/,
                      const std::string &lib_name)
{
    if (!transport_available(transport))
        return;

    if ((transport == "tls" || transport == "wss")
        && !resolve_symbol("zlink_gateway_set_tls_client")) {
        print_result(lib_name, "MULTI_GATEWAY", transport, msg_size, 0.0, 0.0);
        return;
    }

    const multi_bench_settings_t settings = resolve_multi_bench_settings();
    if (settings.clients == 0) {
        print_result(lib_name, "MULTI_GATEWAY", transport, msg_size, 0.0, 0.0);
        return;
    }

    ctx_guard_t ctx;
    if (!ctx.valid())
        return;

    std::string suffix = lib_name + "_gwm_" + transport;
#if !defined(_WIN32)
    suffix += "_" + std::to_string(getpid());
#else
    suffix += "_" + std::to_string(_getpid());
#endif

    const std::string reg_pub = "inproc://gwm_pub_" + suffix;
    const std::string reg_router = "inproc://gwm_router_" + suffix;
    const char *service_name = "svc";

    void *registry = NULL;
    void *discovery = NULL;
    void *gateway = NULL;

    std::vector<void *> providers;
    std::vector<void *> provider_routers;

    auto cleanup = [&]() {
        for (void *provider : providers) {
            if (provider)
                zlink_receiver_destroy(&provider);
        }
        if (gateway)
            zlink_gateway_destroy(&gateway);
        if (discovery)
            zlink_discovery_destroy(&discovery);
        if (registry)
            zlink_registry_destroy(&registry);
    };

    auto fail = [&](double latency = 0.0) {
        print_result(lib_name, "MULTI_GATEWAY", transport, msg_size, 0.0, latency);
        cleanup();
    };

    registry = zlink_registry_new(ctx.get());
    if (!registry)
        return;

    if (zlink_registry_set_endpoints(registry, reg_pub.c_str(), reg_router.c_str())
        != 0
        || zlink_registry_start(registry) != 0) {
        cleanup();
        return;
    }

    discovery = zlink_discovery_new_typed(ctx.get(), ZLINK_SERVICE_TYPE_GATEWAY);
    if (!discovery) {
        cleanup();
        return;
    }
    zlink_discovery_connect_registry(discovery, reg_pub.c_str());
    zlink_discovery_subscribe(discovery, service_name);

    gateway = zlink_gateway_new(ctx.get(), discovery, NULL);
    if (!gateway) {
        cleanup();
        return;
    }

    if (!configure_gateway_tls(gateway, transport)) {
        fail();
        return;
    }

    int base_port = 30000;
#if !defined(_WIN32)
    base_port += (getpid() % 2000);
#else
    base_port += (_getpid() % 2000);
#endif

    for (size_t i = 0; i < providers.size(); ++i) {
        (void)i;
    }

    for (size_t i = 0; i < static_cast<size_t>(settings.clients); ++i) {
        void *provider = zlink_receiver_new(ctx.get(), NULL);
        if (!provider) {
            fail();
            return;
        }

        if (!configure_provider_tls(provider, transport)) {
            zlink_receiver_destroy(&provider);
            fail();
            return;
        }

        std::string provider_endpoint =
          bind_provider(provider, transport, base_port + static_cast<int>(i));
        if (provider_endpoint.empty()) {
            zlink_receiver_destroy(&provider);
            fail();
            return;
        }

        if (zlink_receiver_connect_registry(provider, reg_router.c_str()) != 0
            || zlink_receiver_register(provider, service_name,
                                      provider_endpoint.c_str(), 1)
                   != 0) {
            zlink_receiver_destroy(&provider);
            fail();
            return;
        }

        void *provider_router = zlink_receiver_router(provider);
        if (!provider_router) {
            zlink_receiver_destroy(&provider);
            fail();
            return;
        }

        providers.push_back(provider);
        provider_routers.push_back(provider_router);
    }

    if (!wait_for_discovery(discovery, service_name, 1000)
        || !wait_for_gateway(gateway, service_name, 1000)) {
        fail();
        return;
    }

    settle();

    const int warmup_count = resolve_bench_count("BENCH_WARMUP_COUNT", 200);
    for (int i = 0; i < warmup_count; ++i) {
        if (!send_one_gateway(gateway, service_name, msg_size)
            || !recv_one_provider_any(provider_routers)) {
            fail();
            return;
        }
    }

    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 200);
    stopwatch_t sw;
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        if (!send_one_gateway(gateway, service_name, msg_size)
            || !recv_one_provider_any(provider_routers)) {
            fail();
            return;
        }
    }
    const double latency = (sw.elapsed_ms() * 1000.0) / lat_count;

    std::atomic<int> received(0);
    const auto measure_end =
      std::chrono::steady_clock::now()
      + std::chrono::seconds(std::max(1, settings.measure_seconds));
    const auto drain_end =
      measure_end + std::chrono::milliseconds(std::max(0, settings.drain_ms));

    std::vector<std::thread> receiver_threads;
    receiver_threads.reserve(provider_routers.size());
    for (void *router : provider_routers) {
        receiver_threads.emplace_back([&, router]() {
            while (std::chrono::steady_clock::now() < measure_end) {
                if (recv_one_provider_message_nowait(router))
                    ++received;
                else
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
            while (std::chrono::steady_clock::now() < drain_end) {
                if (recv_one_provider_message_nowait(router))
                    ++received;
                else
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
    }

    sw.start();
    while (std::chrono::steady_clock::now() < measure_end) {
        if (!send_one_gateway(gateway, service_name, msg_size))
            break;
    }

    for (auto &thread : receiver_threads)
        thread.join();

    const int recv_count = received.load();
    const double elapsed_ms = sw.elapsed_ms();
    const double throughput = recv_count > 0 && elapsed_ms > 0
                               ? (double)recv_count / (elapsed_ms / 1000.0)
                               : 0.0;

    print_result(lib_name, "MULTI_GATEWAY", transport, msg_size, throughput, latency);
    cleanup();
}

int main(int argc, char **argv)
{
    return run_standard_bench_main(argc, argv, run_multi_gateway);
}
