#include "../common/bench_common_zlink.hpp"
#include "../common/bench_common_multi.hpp"
#include <zlink.h>
#include <vector>
#include <cstring>
#include <atomic>

static std::string make_client_id(size_t idx)
{
    char id[32];
    std::snprintf(id, sizeof(id), "CLIENT_%zu", idx);
    return std::string(id);
}

void run_multi_router_router(const std::string &transport,
                            size_t msg_size,
                            int /*msg_count*/,
                            const std::string &lib_name)
{
    if (!transport_available(transport))
        return;

    const multi_bench_settings_t settings = resolve_multi_bench_settings();
    if (settings.clients == 0) {
        print_result(lib_name, "MULTI_ROUTER_ROUTER", transport, msg_size, 0.0,
                     0.0);
        return;
    }

    ctx_guard_t ctx;
    if (!ctx.valid())
        return;

    void *server = zlink_socket(ctx.get(), ZLINK_ROUTER);
    if (!server)
        return;
    const int linger_ms = 0;
    set_sockopt_int(server, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");

    const std::string server_id = "ROUTER_SERVER";
    zlink_setsockopt(server, ZLINK_ROUTING_ID, server_id.c_str(), server_id.size());

    int mandatory = 1;
    zlink_setsockopt(server, ZLINK_ROUTER_MANDATORY, &mandatory,
                     sizeof(mandatory));

    std::vector<void *> clients(settings.clients, NULL);
    for (size_t i = 0; i < clients.size(); ++i) {
        clients[i] = zlink_socket(ctx.get(), ZLINK_ROUTER);
        if (!clients[i]) {
            for (size_t j = 0; j < i; ++j)
                zlink_close(clients[j]);
            zlink_close(server);
            return;
        }
        set_sockopt_int(clients[i], ZLINK_LINGER, linger_ms, "ZLINK_LINGER");

        const std::string id = make_client_id(i);
        zlink_setsockopt(clients[i], ZLINK_ROUTING_ID, id.c_str(), id.size());
        zlink_setsockopt(clients[i], ZLINK_ROUTER_MANDATORY, &mandatory,
                         sizeof(mandatory));
    }

    auto cleanup = [&]() {
        for (void *sock : clients) {
            if (sock)
                zlink_close(sock);
        }
        if (server)
            zlink_close(server);
    };

    std::string endpoint =
      bind_and_resolve_endpoint(server, transport,
                               lib_name + "_multi_router_router");
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
    std::vector<char> recv_buf(msg_size + 256);
    char id[256];

    const int warmup_count = resolve_bench_count("BENCH_WARMUP_COUNT", 1000);
    for (int i = 0; i < warmup_count; ++i) {
        const size_t idx = static_cast<size_t>(i % clients.size());
        if (zlink_send(clients[idx], server_id.c_str(), server_id.size(),
                       ZLINK_SNDMORE)
            < 0)
            break;
        if (zlink_send(clients[idx], buffer.data(), msg_size, 0) < 0)
            break;
        if (zlink_recv(server, id, sizeof(id), 0) <= 0)
            break;
        if (zlink_recv(server, recv_buf.data(), recv_buf.size(), 0) < 0)
            break;
    }

    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 1000);
    const double latency = measure_roundtrip_latency_us(lat_count, [&]() {
        if (zlink_send(clients[0], server_id.c_str(), server_id.size(),
                       ZLINK_SNDMORE)
            < 0)
            return;
        if (zlink_send(clients[0], buffer.data(), msg_size, 0) < 0)
            return;

        const int id_len = zlink_recv(server, id, sizeof(id), 0);
        if (id_len <= 0)
            return;
        if (zlink_recv(server, recv_buf.data(), recv_buf.size(), 0) < 0)
            return;

        zlink_send(server, id, id_len, ZLINK_SNDMORE);
        zlink_send(server, buffer.data(), msg_size, 0);
        zlink_recv(clients[0], recv_buf.data(), recv_buf.size(), 0);
    });

    std::atomic<int> sent_count(0);
    const int received = run_multi_timed_benchmark(
      clients,
      settings,
      [&](size_t idx) {
          return zlink_send(clients[idx], server_id.c_str(), server_id.size(),
                            ZLINK_SNDMORE | ZLINK_DONTWAIT) >= 0
                     && zlink_send(clients[idx], buffer.data(), msg_size,
                                   ZLINK_DONTWAIT) >= 0;
      },
      [&]() {
          const int id_len = zlink_recv(server, id, sizeof(id), ZLINK_DONTWAIT);
          if (id_len <= 0)
              return false;
          if (zlink_recv(server, recv_buf.data(), recv_buf.size(), ZLINK_DONTWAIT) < 0) {
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

    print_result(lib_name, "MULTI_ROUTER_ROUTER", transport, msg_size, throughput,
                 latency);
    cleanup();
}

int main(int argc, char **argv)
{
    return run_standard_bench_main(argc, argv, run_multi_router_router);
}
