#include <zlink.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "pattern_dispatch.hpp"

static int gateway_send_one(zlink::gateway_t &gateway, const char *service, size_t size)
{
    std::vector<zlink::message_t> parts;
    parts.emplace_back(size);
    if (size > 0)
        std::memset(parts[0].data(), 'a', size);
    return gateway.send(service, parts);
}

int run_gateway(const std::string &transport, size_t size)
{
    int warmup = env_int("BENCH_WARMUP_COUNT", 200);
    int lat_count = env_int("BENCH_LAT_COUNT", 200);
    int msg_count = resolve_msg_count(size);

    zlink::context_t ctx;

    const std::string suffix =
      std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count());
    const std::string reg_pub = "inproc://gw-pub-" + suffix;
    const std::string reg_router = "inproc://gw-router-" + suffix;
    const char *service = "svc";

    zlink::registry_t registry(ctx);
    if (registry.set_heartbeat(5000, 60000) != 0
        || registry.set_endpoints(reg_pub.c_str(), reg_router.c_str()) != 0
        || registry.start() != 0)
        return 2;

    zlink::discovery_t discovery(ctx, zlink::service_type::gateway);
    if (discovery.connect_registry(reg_pub.c_str()) != 0
        || discovery.subscribe(service) != 0)
        return 2;

    zlink::receiver_t receiver(ctx);
    zlink::gateway_t gateway(ctx, discovery);

    const std::string provider_ep = endpoint_for(transport, "gateway-provider");
    if (receiver.bind(provider_ep.c_str()) != 0
        || receiver.connect_registry(reg_router.c_str()) != 0
        || receiver.register_service(service, provider_ep.c_str(), 1) != 0)
        return 2;

    zlink::socket_t router = zlink::socket_t::wrap(receiver.router_handle());
    if (!router.handle())
        return 2;

    const auto wait_deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
    while (std::chrono::steady_clock::now() < wait_deadline) {
        if (discovery.receiver_count(service) > 0 && gateway.connection_count(service) > 0)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    settle();

    std::vector<char> rid(256);
    std::vector<char> data(size > 256 ? size : 256);
    for (int i = 0; i < warmup; ++i) {
        if (gateway_send_one(gateway, service, size) != 0
            || router.recv(rid.data(), rid.size()) < 0
            || router.recv(data.data(), data.size()) < 0)
            return 2;
    }

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < lat_count; ++i) {
        if (gateway_send_one(gateway, service, size) != 0
            || router.recv(rid.data(), rid.size()) < 0
            || router.recv(data.data(), data.size()) < 0)
            return 2;
    }
    double latency = static_cast<double>(
      std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0).count()) / lat_count;

    std::atomic<int> recv_count(0);
    std::thread rcv([&]() {
        for (int i = 0; i < msg_count; ++i) {
            if (router.recv(rid.data(), rid.size()) < 0 || router.recv(data.data(), data.size()) < 0)
                break;
            ++recv_count;
        }
    });

    int sent = 0;
    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < msg_count; ++i) {
        if (gateway_send_one(gateway, service, size) != 0)
            break;
        ++sent;
    }
    rcv.join();
    const int eff = sent < recv_count.load() ? sent : recv_count.load();
    const double elapsed_s = static_cast<double>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - t0).count()) / 1e9;
    const double throughput = (eff > 0 && elapsed_s > 0.0) ? (eff / elapsed_s) : 0.0;

    print_result("GATEWAY", transport, size, throughput, latency);
    return 0;
}

int run_pattern_gateway(const std::string &transport, size_t size)
{
    return run_gateway(transport, size);
}
