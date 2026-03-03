#include "../common/bench_common.hpp"
#include <chrono>
#include <zmq.h>
#include <atomic>
#include <thread>
#include <vector>
#include <cstring>

#ifndef ZLINK_TCP_NODELAY
#define ZLINK_TCP_NODELAY 26
#endif

namespace {

bool run_roundtrip_once(void *sender,
                        void *receiver,
                        const std::vector<char> &buffer,
                        size_t msg_size)
{
    return send_exact(sender, buffer.data(), msg_size, 0)
           && recv_single_part_msg_flags(receiver, msg_size, 0) > 0
           && send_exact(receiver, buffer.data(), msg_size, 0)
           && recv_single_part_msg_flags(sender, msg_size, 0) > 0;
}

bool run_warmup_roundtrip(void *sender,
                          void *receiver,
                          const std::vector<char> &buffer,
                          size_t msg_size,
                          int warmup_count)
{
    for (int i = 0; i < warmup_count; ++i) {
        if (!run_roundtrip_once(sender, receiver, buffer, msg_size)) {
            return false;
        }
    }
    return true;
}

bool run_latency_roundtrip(void *sender,
                           void *receiver,
                           const std::vector<char> &buffer,
                           size_t msg_size,
                           latency_stats_t *out_stats)
{
    if (!out_stats)
        return false;

    const int latency_duration_s = resolve_single_latency_duration_seconds();
    latency_stats_builder_t latency_builder;
    const auto latency_deadline =
      std::chrono::steady_clock::now()
      + std::chrono::seconds(latency_duration_s > 0 ? latency_duration_s : 1);
    while (std::chrono::steady_clock::now() < latency_deadline) {
        stopwatch_t sw;
        sw.start();
        if (!run_roundtrip_once(sender, receiver, buffer, msg_size)) {
            return false;
        }
        latency_builder.add((sw.elapsed_ms() * 1000.0) * 0.5);
    }

    if (latency_builder.count() == 0)
        return false;

    *out_stats = latency_builder.snapshot();
    return true;
}

bool run_throughput_parallel(void *sender,
                             void *receiver,
                             const std::vector<char> &buffer,
                             size_t msg_size,
                             queue_probe_t &queue_probe,
                             double *out_throughput)
{
    if (!out_throughput)
        return false;

    const int throughput_duration_s = resolve_single_duration_seconds();
    const auto throughput_deadline =
      std::chrono::steady_clock::now()
      + std::chrono::seconds(throughput_duration_s > 0 ? throughput_duration_s
                                                       : 1);
    const int recv_timeout_ms = resolve_single_recv_timeout_ms();
    const auto drain_idle_limit =
      std::chrono::milliseconds(recv_timeout_ms > 0 ? recv_timeout_ms : 200);

    std::atomic<bool> sender_done(false);
    std::atomic<bool> recv_failed(false);
    std::atomic<int> recv_in_window(0);

    std::thread receiver_thread([&]() {
        auto last_recv_at = std::chrono::steady_clock::now();
        queue_probe.force_sample_recv();
        while (true) {
            const bool done = sender_done.load(std::memory_order_acquire);
            const int flags = done ? ZLINK_DONTWAIT : 0;
            const int recv_rc =
              recv_single_part_msg_flags(receiver, msg_size, flags);
            if (recv_rc <= 0) {
                if (recv_rc == 0) {
                    if (done
                        && std::chrono::steady_clock::now() - last_recv_at
                             >= drain_idle_limit) {
                        break;
                    }
                    std::this_thread::yield();
                    continue;
                }
                recv_failed.store(true, std::memory_order_release);
                break;
            }

            last_recv_at = std::chrono::steady_clock::now();
            if (std::chrono::steady_clock::now() < throughput_deadline)
                recv_in_window.fetch_add(1, std::memory_order_release);
            queue_probe.sample_recv_if_due();

            // Drain immediately available messages in a non-blocking burst.
            for (;;) {
                const int burst_rc = recv_single_part_msg_flags(
                  receiver, msg_size, ZLINK_DONTWAIT);
                if (burst_rc > 0) {
                    last_recv_at = std::chrono::steady_clock::now();
                    if (std::chrono::steady_clock::now() < throughput_deadline)
                        recv_in_window.fetch_add(1, std::memory_order_release);
                    queue_probe.sample_recv_if_due();
                    continue;
                }
                if (burst_rc == 0) {
                    break;
                }

                recv_failed.store(true, std::memory_order_release);
                break;
            }

            if (recv_failed.load(std::memory_order_acquire))
                break;
        }
        queue_probe.force_sample_recv();
    });

    bool send_failed = false;
    queue_probe.force_sample_send();
    while (std::chrono::steady_clock::now() < throughput_deadline) {
        if (!send_exact(sender, buffer.data(), msg_size, 0)) {
            send_failed = true;
            break;
        }
        queue_probe.sample_send_if_due();
    }
    queue_probe.force_sample_send();
    sender_done.store(true, std::memory_order_release);
    receiver_thread.join();

    if (send_failed || recv_failed.load(std::memory_order_acquire))
        return false;

    const int received = recv_in_window.load(std::memory_order_acquire);
    if (received <= 0)
        return false;

    *out_throughput =
      static_cast<double>(received) / static_cast<double>(throughput_duration_s);
    return true;
}

} // namespace

void run_pair(const std::string& transport,
              size_t msg_size,
              const std::string& lib_name) {
    if (!transport_available(transport))
        return;

    auto print_fail_no_queue = [&]() {
        print_fail_result(lib_name, "PAIR", transport, msg_size);
    };

    ctx_guard_t ctx;
    if (!ctx.valid()) {
        print_fail_no_queue();
        return;
    }

    socket_guard_t s_bind(ctx.get(), ZLINK_PAIR);
    socket_guard_t s_conn(ctx.get(), ZLINK_PAIR);
    if (!s_bind.valid() || !s_conn.valid()) {
        print_fail_no_queue();
        return;
    }

    int nodelay = 1;
    set_sockopt_int(s_bind.get(), ZLINK_TCP_NODELAY, nodelay,
                    "ZLINK_TCP_NODELAY");
    set_sockopt_int(s_conn.get(), ZLINK_TCP_NODELAY, nodelay,
                    "ZLINK_TCP_NODELAY");

    if (!setup_connected_pair(s_bind.get(), s_conn.get(), transport,
                              lib_name + "_pair")) {
        print_fail_no_queue();
        return;
    }

    std::vector<char> buffer(msg_size, 'a');
    queue_probe_t queue_probe(s_conn.get(), s_bind.get());

    auto print_fail_with_queue = [&]() {
        print_fail_result(lib_name, "PAIR", transport, msg_size, &queue_probe);
    };

    const int recv_timeout_ms = resolve_single_recv_timeout_ms();
    set_sockopt_int(s_bind.get(), ZLINK_RCVTIMEO, recv_timeout_ms,
                    "ZLINK_RCVTIMEO");
    set_sockopt_int(s_conn.get(), ZLINK_RCVTIMEO, recv_timeout_ms,
                    "ZLINK_RCVTIMEO");

    const int warmup_count = resolve_bench_count("PERF_WARMUP_COUNT", 1000);
    if (!run_warmup_roundtrip(s_conn.get(), s_bind.get(), buffer, msg_size,
                              warmup_count)) {
        print_fail_with_queue();
        return;
    }

    latency_stats_t latency_stats;
    if (!run_latency_roundtrip(s_conn.get(), s_bind.get(), buffer, msg_size,
                               &latency_stats)) {
        print_fail_with_queue();
        return;
    }

    double throughput = 0.0;
    if (!run_throughput_parallel(s_conn.get(), s_bind.get(), buffer, msg_size,
                                 queue_probe, &throughput)) {
        print_fail_with_queue();
        return;
    }

    const queue_stats_t queue_stats = queue_probe.snapshot();

    print_result(lib_name, "PAIR", transport, msg_size, throughput,
                 latency_stats.mean_us, latency_stats.p95_us,
                 latency_stats.p99_us, queue_stats);
}

int main(int argc, char** argv) {
    return run_standard_bench_main(argc, argv, run_pair);
}
