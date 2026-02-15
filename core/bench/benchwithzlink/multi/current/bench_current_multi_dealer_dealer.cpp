#include "../common/bench_common.hpp"
#include "../common/bench_common_multi.hpp"
#include <zlink.h>
#include <vector>
#include <cstring>
#include <atomic>
#include <cerrno>

void run_multi_dealer_dealer(const std::string& transport,
                            size_t msg_size,
                            int /*msg_count*/,
                            const std::string& lib_name)
{
    if (!transport_available(transport))
        return;

    const multi_bench_settings_t settings = resolve_multi_bench_settings();
    if (settings.clients == 0) {
        print_result(lib_name, "MULTI_DEALER_DEALER", transport, msg_size, 0.0,
                     0.0);
        return;
    }

    ctx_guard_t ctx;
    if (!ctx.valid())
        return;

    void *server = zlink_socket(ctx.get(), ZLINK_DEALER);
    if (!server)
        return;

    std::vector<void *> clients(settings.clients, NULL);
    for (size_t i = 0; i < clients.size(); ++i) {
        clients[i] = zlink_socket(ctx.get(), ZLINK_DEALER);
        if (!clients[i]) {
            for (size_t j = 0; j < i; ++j)
                zlink_close(clients[j]);
            zlink_close(server);
            return;
        }
    }

    auto cleanup = [&]() {
        for (void *sock : clients) {
            if (sock)
                zlink_close(sock);
        }
        if (server)
            zlink_close(server);
    };

    if (!setup_tls_server(server, transport)) {
        cleanup();
        return;
    }
    for (void *sock : clients) {
        if (!setup_tls_client(sock, transport)) {
            cleanup();
            return;
        }
    }

    std::string endpoint =
      bind_and_resolve_endpoint(server, transport, lib_name + "_multi_dealer_dealer");
    if (endpoint.empty() ||
        !connect_clients_concurrently(
          clients,
          endpoint,
          [](void *sock, const std::string &ep) {
              return connect_checked(sock, ep);
          },
          settings.connect_concurrency)) {
        cleanup();
        return;
    }

    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(std::max<size_t>(1, msg_size));

    const int warmup_count = resolve_bench_count("BENCH_WARMUP_COUNT", 1000);
    for (int i = 0; i < warmup_count; ++i) {
        const size_t idx = static_cast<size_t>(i % clients.size());
        if (zlink_send(clients[idx], buffer.data(), msg_size, 0) < 0)
            break;
        if (zlink_recv(server, recv_buf.data(), recv_buf.size(), 0) < 0)
            break;
    }

    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 500);
    auto recv_any_client = [&](int recv_timeout_ms) -> bool {
        auto deadline =
          std::chrono::steady_clock::now()
          + std::chrono::milliseconds(std::max(1, recv_timeout_ms));
        while (std::chrono::steady_clock::now() < deadline) {
            for (size_t i = 0; i < clients.size(); ++i) {
                if (zlink_recv(clients[i], recv_buf.data(), recv_buf.size(),
                               ZLINK_DONTWAIT)
                    >= 0) {
                    return true;
                }
                if (zlink_errno() != EAGAIN)
                    return false;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
        return false;
    };

    const double latency = measure_roundtrip_latency_us(lat_count, [&]() {
        const size_t idx = 0;
        if (zlink_send(clients[idx], buffer.data(), msg_size, 0) < 0)
            return;
        if (zlink_recv(server, recv_buf.data(), recv_buf.size(), 0) < 0)
            return;
        if (zlink_send(server, recv_buf.data(), recv_buf.size(), 0) < 0)
            return;
        recv_any_client(200);
    });

    std::atomic<int> sent_count(0);
    const int received = run_multi_timed_benchmark(
      clients,
      settings,
      [&](size_t idx) {
          return zlink_send(clients[idx], buffer.data(), msg_size, 0) >= 0;
      },
      [&]() {
          if (zlink_recv(server, recv_buf.data(), recv_buf.size(),
                        ZLINK_DONTWAIT) < 0) {
              if (zlink_errno() != EAGAIN)
                  return false;
              return false;
          }
          return true;
      },
      settings.measure_seconds,
      &sent_count);

    const double throughput =
      received > 0
        ? static_cast<double>(received)
            / static_cast<double>(std::max(1, settings.measure_seconds))
        : 0.0;

    print_result(lib_name, "MULTI_DEALER_DEALER", transport, msg_size, throughput,
                 latency);
    cleanup();
}

int main(int argc, char **argv)
{
    return run_standard_bench_main(argc, argv, run_multi_dealer_dealer);
}
