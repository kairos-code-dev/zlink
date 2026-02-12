#include <zlink.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "pattern_dispatch.hpp"

int run_stream(const std::string &transport, size_t size)
{
    int warmup = env_int("BENCH_WARMUP_COUNT", 1000);
    int lat_count = env_int("BENCH_LAT_COUNT", 500);
    int msg_count = resolve_msg_count(size);

    zlink::context_t ctx;
    zlink::socket_t server(ctx, zlink::socket_type::stream);
    zlink::socket_t client(ctx, zlink::socket_type::stream);

    std::string endpoint = endpoint_for(transport, "stream");
    if (server.bind(endpoint) != 0 || client.connect(endpoint) != 0)
        return 2;

    settle();

    std::vector<unsigned char> server_client_id = stream_expect_connect_event(server);
    std::vector<unsigned char> client_server_id = stream_expect_connect_event(client);
    if (server_client_id.empty() || client_server_id.empty())
        return 2;

    std::vector<char> buffer(size, 'a');
    std::vector<char> recv_buf(size > 256 ? size : 256);

    for (int i = 0; i < warmup; ++i) {
        if (stream_send(client, client_server_id, buffer.data(), size) < 0
            || stream_recv(server, NULL, recv_buf.data(), recv_buf.size()) < 0)
            return 2;
    }

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < lat_count; ++i) {
        if (stream_send(client, client_server_id, buffer.data(), size) < 0
            || stream_recv(server, NULL, recv_buf.data(), recv_buf.size()) < 0
            || stream_send(server, server_client_id, recv_buf.data(), size) < 0
            || stream_recv(client, NULL, recv_buf.data(), recv_buf.size()) < 0)
            return 2;
    }

    double latency =
      static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count())
      / (lat_count * 2);

    std::atomic<bool> recv_ok(true);
    std::thread receiver([&]() {
        for (int i = 0; i < msg_count; ++i) {
            if (stream_recv(server, NULL, recv_buf.data(), recv_buf.size()) < 0) {
                recv_ok.store(false);
                return;
            }
        }
    });

    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < msg_count; ++i) {
        if (stream_send(client, client_server_id, buffer.data(), size) < 0) {
            recv_ok.store(false);
            break;
        }
    }
    receiver.join();

    if (!recv_ok.load())
        return 2;

    double throughput =
      static_cast<double>(msg_count)
      / (static_cast<double>(
           std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now() - t0)
             .count())
         / 1e9);

    print_result("STREAM", transport, size, throughput, latency);
    return 0;
}

int run_pattern_stream(const std::string &transport, size_t size)
{
    return run_stream(transport, size);
}
