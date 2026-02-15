#include "../common/bench_common.hpp"
#include "../common/bench_common_multi.hpp"
#include <zmq.h>
#include <vector>
#include <atomic>

void run_multi_pubsub(const std::string& transport,
                     size_t msg_size,
                     int /*msg_count*/,
                     const std::string& lib_name)
{
    if (!transport_available(transport))
        return;

    const multi_bench_settings_t settings = resolve_multi_bench_settings();
    if (settings.clients == 0) {
        print_result(lib_name, "MULTI_PUBSUB", transport, msg_size, 0.0, 0.0);
        return;
    }

    ctx_guard_t ctx;
    if (!ctx.valid())
        return;

    void *pub = zmq_socket(ctx.get(), ZMQ_PUB);
    if (!pub)
        return;
    const int linger_ms = 0;
    set_sockopt_int(pub, ZMQ_LINGER, linger_ms, "ZMQ_LINGER");

    std::vector<void *> subs(settings.clients, NULL);
    for (size_t i = 0; i < subs.size(); ++i) {
        subs[i] = zmq_socket(ctx.get(), ZMQ_SUB);
        if (!subs[i]) {
            for (size_t j = 0; j < i; ++j)
                zmq_close(subs[j]);
            zmq_close(pub);
            return;
        }
        zmq_setsockopt(subs[i], ZMQ_SUBSCRIBE, "", 0);
        set_sockopt_int(subs[i], ZMQ_LINGER, linger_ms, "ZMQ_LINGER");
    }

    auto cleanup = [&]() {
        for (void *sock : subs) {
            if (sock)
                zmq_close(sock);
        }
        if (pub)
            zmq_close(pub);
    };

    const bool is_pgm = transport == "pgm" || transport == "epgm";
    int poll_timeout_ms = 50;
    if (is_pgm) {
        poll_timeout_ms =
          resolve_bench_count("BENCH_PGM_POLL_TIMEOUT_MS", 50);
        const char *cap = transport == "pgm" ? "pgm" : "epgm";
        if (!zmq_has(cap)) {
            print_result(lib_name, "MULTI_PUBSUB", transport, msg_size, 0.0, 0.0);
            cleanup();
            return;
        }

        const int timeout_ms = poll_timeout_ms;
        set_sockopt_int(pub, ZMQ_SNDTIMEO, timeout_ms, "ZMQ_SNDTIMEO");
        for (void *sub : subs)
            set_sockopt_int(sub, ZMQ_RCVTIMEO, timeout_ms, "ZMQ_RCVTIMEO");
    } else {
        poll_timeout_ms = resolve_bench_count("BENCH_PUBSUB_POLL_TIMEOUT_MS", 50);
    }

    std::string endpoint =
      bind_and_resolve_endpoint(pub, transport, lib_name + "_multi_pubsub");
    if (endpoint.empty() ||
        !connect_clients_concurrently(
          subs,
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
        zmq_send(pub, buffer.data(), msg_size, 0);
        zmq_pollitem_t items[] = {{subs[0], 0, ZMQ_POLLIN, 0}};
        if (zmq_poll(items, 1, poll_timeout_ms) > 0 &&
            (items[0].revents & ZMQ_POLLIN)) {
            zmq_recv(subs[0], recv_buf.data(), recv_buf.size(), 0);
        }
    }

    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 500);
    auto lat_start = std::chrono::steady_clock::now();
    for (int i = 0; i < lat_count; ++i) {
        zmq_send(pub, buffer.data(), msg_size, 0);
        zmq_pollitem_t items[] = {{subs[0], 0, ZMQ_POLLIN, 0}};
        if (zmq_poll(items, 1, poll_timeout_ms) <= 0 ||
            !(items[0].revents & ZMQ_POLLIN) ||
            zmq_recv(subs[0], recv_buf.data(), recv_buf.size(), 0) < 0) {
            // continue
        }
    }
    const auto lat_elapsed = std::chrono::steady_clock::now() - lat_start;
    const double latency =
      (std::chrono::duration<double, std::milli>(lat_elapsed).count() * 1000.0)
      / std::max(1, lat_count);

    std::vector<void *> publishers(1, pub);
    const int received_count = run_multi_timed_benchmark(
      publishers,
      settings,
      [&](size_t) {
          return zmq_send(pub, buffer.data(), msg_size, ZMQ_DONTWAIT) >= 0;
      },
      [&]() {
          for (void *sub : subs) {
              zmq_pollitem_t items[] = {{sub, 0, ZMQ_POLLIN, 0}};
              if (zmq_poll(items, 1, 0) > 0 &&
                  (items[0].revents & ZMQ_POLLIN)) {
                  if (zmq_recv(sub, recv_buf.data(), recv_buf.size(), 0) >= 0)
                      return true;
              }
          }
          return false;
      },
      settings.measure_seconds,
      nullptr);

    const double throughput =
      received_count > 0
        ? static_cast<double>(received_count)
            / static_cast<double>(std::max(1, settings.measure_seconds))
        : 0.0;

    print_result(lib_name, "MULTI_PUBSUB", transport, msg_size, throughput,
                 latency);
    cleanup();
}

int main(int argc, char **argv)
{
    return run_standard_bench_main(argc, argv, run_multi_pubsub);
}
