#include "../common/bench_common.hpp"
#include <zlink.h>
#include <atomic>
#include <vector>
#include <cstring>
#include <cstdlib>

inline int resolve_pubsub_xpub_nodrop_opt()
{
    const char *env = std::getenv("PERF_SINGLE_PUBSUB_XPUB_NODROP");
    if (!env || !*env)
        return 1;

    if (strcmp(env, "1") == 0 || strcmp(env, "true") == 0
        || strcmp(env, "TRUE") == 0 || strcmp(env, "on") == 0
        || strcmp(env, "ON") == 0) {
        return 1;
    }
    if (strcmp(env, "0") == 0 || strcmp(env, "false") == 0
        || strcmp(env, "FALSE") == 0 || strcmp(env, "off") == 0
        || strcmp(env, "OFF") == 0) {
        return 0;
    }
    return 1;
}

inline int recv_sub_blocking_with_timeout (void *sub_socket,
                                           size_t msg_size)
{
    return recv_single_part_msg_flags(sub_socket, msg_size, 0);
}

static bool run_pubsub_warmup_and_latency(void *pub_socket,
                                          void *sub_socket,
                                          const std::vector<char> &buffer,
                                          size_t msg_size,
                                          latency_stats_t *out_stats)
{
    if (!out_stats)
        return false;

    const int warmup_count = resolve_bench_count("PERF_WARMUP_COUNT", 1000);
    for (int i = 0; i < warmup_count; ++i) {
        if (!send_exact(pub_socket, buffer.data(), msg_size, 0))
            return false;
        if (recv_sub_blocking_with_timeout(sub_socket, msg_size) < 0)
            return false;
    }

    const int latency_duration_s = resolve_single_latency_duration_seconds();
    latency_stats_builder_t latency_builder;
    const auto latency_deadline =
      std::chrono::steady_clock::now()
      + std::chrono::seconds(latency_duration_s > 0 ? latency_duration_s : 1);
    while (std::chrono::steady_clock::now() < latency_deadline) {
        stopwatch_t per_message;
        per_message.start();
        if (!send_exact(pub_socket, buffer.data(), msg_size, 0))
            return false;
        const int recv_rc = recv_sub_blocking_with_timeout(sub_socket, msg_size);
        if (recv_rc < 0)
            return false;
        if (recv_rc > 0)
            latency_builder.add(per_message.elapsed_ms() * 1000.0);
    }

    if (latency_builder.count() == 0)
        return false;

    *out_stats = latency_builder.snapshot();
    return true;
}

static bool run_pubsub_throughput_parallel(void *pub_socket,
                                           void *sub_socket,
                                           const std::vector<char> &buffer,
                                           size_t msg_size,
                                           int recv_timeout_ms,
                                           int throughput_duration_s,
                                           queue_probe_t &queue_probe,
                                           double *out_throughput)
{
    if (!out_throughput)
        return false;

    std::atomic<bool> sender_done(false);
    std::atomic<bool> recv_failed(false);
    std::atomic<int> recv_count(0);
    const auto throughput_deadline =
      std::chrono::steady_clock::now()
      + std::chrono::seconds(throughput_duration_s > 0 ? throughput_duration_s
                                                       : 1);
    const auto drain_idle_limit =
      std::chrono::milliseconds(recv_timeout_ms > 0 ? recv_timeout_ms : 200);
    std::thread receiver([&]() {
        auto last_recv_at = std::chrono::steady_clock::now();
        queue_probe.force_sample_recv();
        while (true) {
            const bool done = sender_done.load(std::memory_order_acquire);
            const int flags = done ? ZLINK_DONTWAIT : 0;
            const int recv_rc =
              recv_single_part_msg_flags(sub_socket, msg_size, flags);
            if (recv_rc > 0) {
                last_recv_at = std::chrono::steady_clock::now();
                if (std::chrono::steady_clock::now() < throughput_deadline) {
                    recv_count.fetch_add(1, std::memory_order_release);
                }
                queue_probe.sample_recv_if_due();

                // Drain immediately available messages in a non-blocking burst.
                for (;;) {
                    const int burst_rc =
                      recv_single_part_msg_flags(sub_socket, msg_size,
                                                 ZLINK_DONTWAIT);
                    if (burst_rc > 0) {
                        last_recv_at = std::chrono::steady_clock::now();
                        if (std::chrono::steady_clock::now()
                            < throughput_deadline) {
                            recv_count.fetch_add(1, std::memory_order_release);
                        }
                        queue_probe.sample_recv_if_due();
                        continue;
                    }
                    if (burst_rc == 0)
                        break;

                    recv_failed.store(true, std::memory_order_release);
                    break;
                }
                if (recv_failed.load(std::memory_order_acquire))
                    break;
                continue;
            }

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
        queue_probe.force_sample_recv();
    });

    queue_probe.force_sample_send();
    bool send_failed = false;
    while (std::chrono::steady_clock::now() < throughput_deadline) {
        if (!send_exact(pub_socket, buffer.data(), msg_size, 0)) {
            send_failed = true;
            break;
        }
        queue_probe.sample_send_if_due();
    }
    queue_probe.force_sample_send();
    sender_done.store(true, std::memory_order_release);
    receiver.join();

    if (send_failed || recv_failed.load(std::memory_order_acquire))
        return false;

    const int received = recv_count.load(std::memory_order_acquire);
    if (received <= 0)
        return false;

    *out_throughput = static_cast<double>(received)
                      / static_cast<double>(throughput_duration_s);
    return true;
}

void run_pubsub(const std::string& transport,
                size_t msg_size,
                const std::string& lib_name) {
    if (!transport_available(transport))
        return;

    auto print_fail_no_queue = [&]() {
        print_fail_result(lib_name, "PUBSUB", transport, msg_size);
    };

    ctx_guard_t ctx;
    if (!ctx.valid()) {
        print_fail_no_queue();
        return;
    }

    socket_guard_t pub(ctx.get(), ZLINK_PUB);
    socket_guard_t sub(ctx.get(), ZLINK_SUB);
    if (!pub.valid() || !sub.valid()) {
        print_fail_no_queue();
        return;
    }

    const int xpub_nodrop_opt = resolve_pubsub_xpub_nodrop_opt();
    set_sockopt_int(pub.get(), ZLINK_XPUB_NODROP, xpub_nodrop_opt,
                    "ZLINK_XPUB_NODROP");

    zlink_setsockopt(sub.get(), ZLINK_SUBSCRIBE, "", 0);

    if (!setup_connected_pair(pub.get(), sub.get(), transport,
                              lib_name + "_pubsub")) {
        print_fail_no_queue();
        return;
    }
    const int recv_timeout_ms = resolve_single_pubsub_recv_timeout_ms();
    set_sockopt_int(
      sub.get(), ZLINK_RCVTIMEO, recv_timeout_ms, "ZLINK_RCVTIMEO");

    std::vector<char> buffer(msg_size, 'a');
    queue_probe_t queue_probe(pub.get(), sub.get());

    auto print_fail_with_queue = [&]() {
        print_fail_result(lib_name, "PUBSUB", transport, msg_size, &queue_probe);
    };

    const int throughput_duration_s = resolve_single_duration_seconds();
    latency_stats_t latency_stats;
    if (!run_pubsub_warmup_and_latency(pub.get(), sub.get(), buffer, msg_size,
                                       &latency_stats)) {
        print_fail_with_queue();
        return;
    }

    double throughput = 0.0;
    if (!run_pubsub_throughput_parallel(pub.get(), sub.get(), buffer, msg_size,
                                        recv_timeout_ms,
                                        throughput_duration_s, queue_probe,
                                        &throughput)) {
        print_fail_with_queue();
        return;
    }

    const queue_stats_t queue_stats = queue_probe.snapshot();
    print_result(lib_name, "PUBSUB", transport, msg_size, throughput,
                 latency_stats.mean_us, latency_stats.p95_us,
                 latency_stats.p99_us, queue_stats);
}

int main(int argc, char** argv) {
    return run_standard_bench_main(argc, argv, run_pubsub);
}
