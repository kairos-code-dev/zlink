#include "../common/bench_common.hpp"
#include <zlink.h>
#include <atomic>
#include <thread>
#include <vector>
#include <cstring>

namespace {

bool run_warmup_router_echo(void *dealer,
                            void *router,
                            const std::vector<char> &buffer,
                            size_t msg_size,
                            int warmup_count)
{
    std::atomic<bool> worker_failed(false);
    std::thread worker([&]() {
        for (int i = 0; i < warmup_count; ++i) {
            std::vector<char> routing_id;
            const int recv_rc =
              recv_two_part_msg_flags(router, msg_size, &routing_id, 0);
            if (recv_rc <= 0
                || !send_exact(router, routing_id.data(), routing_id.size(),
                               ZLINK_SNDMORE)
                || !send_exact(router, buffer.data(), msg_size, 0)) {
                worker_failed.store(true, std::memory_order_release);
                break;
            }
        }
    });

    bool main_failed = false;
    for (int i = 0; i < warmup_count; ++i) {
        if (!send_exact(dealer, buffer.data(), msg_size, 0)
            || recv_single_part_msg_flags(dealer, msg_size, 0) <= 0) {
            main_failed = true;
            break;
        }
    }

    worker.join();
    return !main_failed && !worker_failed.load(std::memory_order_acquire);
}

bool run_latency_router_echo(void *dealer,
                             void *router,
                             const std::vector<char> &buffer,
                             size_t msg_size,
                             latency_stats_t *out_stats)
{
    if (!out_stats)
        return false;

    std::atomic<bool> worker_stop(false);
    std::atomic<bool> worker_failed(false);
    const int recv_timeout_ms = resolve_single_recv_timeout_ms();
    const auto drain_idle_limit =
      std::chrono::milliseconds(recv_timeout_ms > 0 ? recv_timeout_ms : 200);
    std::thread worker([&]() {
        auto last_recv_at = std::chrono::steady_clock::now();
        while (true) {
            const bool stop = worker_stop.load(std::memory_order_acquire);
            const int flags = stop ? ZLINK_DONTWAIT : 0;
            std::vector<char> routing_id;
            const int recv_rc =
              recv_two_part_msg_flags(router, msg_size, &routing_id, flags);
            if (recv_rc > 0) {
                last_recv_at = std::chrono::steady_clock::now();
                if (!send_exact(router, routing_id.data(), routing_id.size(),
                                ZLINK_SNDMORE)
                    || !send_exact(router, buffer.data(), msg_size, 0)) {
                    worker_failed.store(true, std::memory_order_release);
                    break;
                }

                // Drain immediately available messages in a non-blocking burst.
                for (;;) {
                    std::vector<char> burst_routing_id;
                    const int burst_rc = recv_two_part_msg_flags(
                      router, msg_size, &burst_routing_id, ZLINK_DONTWAIT);
                    if (burst_rc > 0) {
                        last_recv_at = std::chrono::steady_clock::now();
                        if (!send_exact(router, burst_routing_id.data(),
                                        burst_routing_id.size(), ZLINK_SNDMORE)
                            || !send_exact(
                              router, buffer.data(), msg_size, 0)) {
                            worker_failed.store(true, std::memory_order_release);
                            break;
                        }
                        continue;
                    }
                    if (burst_rc == 0)
                        break;
                    worker_failed.store(true, std::memory_order_release);
                    break;
                }
                if (worker_failed.load(std::memory_order_acquire))
                    break;
                continue;
            }
            if (recv_rc == 0) {
                if (stop
                    && std::chrono::steady_clock::now() - last_recv_at
                         >= drain_idle_limit) {
                    break;
                }
                std::this_thread::yield();
                continue;
            }
            worker_failed.store(true, std::memory_order_release);
            break;
        }
    });

    const int latency_duration_s = resolve_single_latency_duration_seconds();
    latency_stats_builder_t latency_builder;
    const auto latency_deadline =
      std::chrono::steady_clock::now()
      + std::chrono::seconds(latency_duration_s > 0 ? latency_duration_s : 1);
    bool main_failed = false;
    while (std::chrono::steady_clock::now() < latency_deadline) {
        stopwatch_t sw;
        sw.start();
        if (!send_exact(dealer, buffer.data(), msg_size, 0)
            || recv_single_part_msg_flags(dealer, msg_size, 0) <= 0) {
            main_failed = true;
            break;
        }
        latency_builder.add((sw.elapsed_ms() * 1000.0) * 0.5);
    }

    worker_stop.store(true, std::memory_order_release);
    worker.join();
    if (main_failed || worker_failed.load(std::memory_order_acquire)
        || latency_builder.count() == 0) {
        return false;
    }

    *out_stats = latency_builder.snapshot();
    return true;
}

bool run_throughput_router_recv(void *dealer,
                                void *router,
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
    std::atomic<bool> sender_done(false);
    std::atomic<int> sent_count(0);
    std::atomic<bool> recv_failed(false);
    std::atomic<int> recv_total(0);
    std::atomic<int> recv_in_window(0);

    std::thread receiver_thread([&]() {
        queue_probe.force_sample_recv();
        while (!sender_done.load(std::memory_order_acquire)
               || recv_total.load(std::memory_order_acquire)
                    < sent_count.load(std::memory_order_acquire)) {
            const bool done = sender_done.load(std::memory_order_acquire);
            const int flags = done ? ZLINK_DONTWAIT : 0;
            const int recv_rc =
              recv_two_part_msg_flags(router, msg_size, NULL, flags);
            if (recv_rc > 0) {
                recv_total.fetch_add(1, std::memory_order_release);
                if (std::chrono::steady_clock::now() < throughput_deadline)
                    recv_in_window.fetch_add(1, std::memory_order_release);
                queue_probe.sample_recv_if_due();

                // Drain immediately available messages in a non-blocking burst.
                for (;;) {
                    const int burst_rc = recv_two_part_msg_flags(
                      router, msg_size, NULL, ZLINK_DONTWAIT);
                    if (burst_rc > 0) {
                        recv_total.fetch_add(1, std::memory_order_release);
                        if (std::chrono::steady_clock::now()
                            < throughput_deadline) {
                            recv_in_window.fetch_add(
                              1, std::memory_order_release);
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
                std::this_thread::yield();
                continue;
            }
            recv_failed.store(true, std::memory_order_release);
            break;
        }
        queue_probe.force_sample_recv();
    });

    bool send_failed = false;
    queue_probe.force_sample_send();
    while (std::chrono::steady_clock::now() < throughput_deadline) {
        if (!send_exact(dealer, buffer.data(), msg_size, 0)) {
            send_failed = true;
            break;
        }
        sent_count.fetch_add(1, std::memory_order_release);
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

bool setup_dealer_router_session(void *router,
                                 void *dealer,
                                 const std::string &transport,
                                 const std::string &pair_id,
                                 int recv_timeout_ms)
{
    if (!router || !dealer)
        return false;

    // Set Routing ID for Dealer.
    zlink_setsockopt(dealer, ZLINK_ROUTING_ID, "CLIENT", 6);

    if (!setup_connected_pair(router, dealer, transport, pair_id))
        return false;

    // Give it time to connect.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    set_sockopt_int(router, ZLINK_RCVTIMEO, recv_timeout_ms, "ZLINK_RCVTIMEO");
    set_sockopt_int(dealer, ZLINK_RCVTIMEO, recv_timeout_ms, "ZLINK_RCVTIMEO");
    return true;
}

} // namespace

void run_dealer_router(const std::string& transport,
                       size_t msg_size,
                       const std::string& lib_name) {
    if (!transport_available(transport))
        return;

    auto print_fail_no_queue = [&]() {
        print_fail_result(lib_name, "DEALER_ROUTER", transport, msg_size);
    };

    ctx_guard_t ctx;
    if (!ctx.valid()) {
        print_fail_no_queue();
        return;
    }

    socket_guard_t router(ctx.get(), ZLINK_ROUTER);
    socket_guard_t dealer(ctx.get(), ZLINK_DEALER);
    if (!router.valid() || !dealer.valid()) {
        print_fail_no_queue();
        return;
    }

    const int recv_timeout_ms = resolve_single_recv_timeout_ms();
    if (!setup_dealer_router_session(router.get(), dealer.get(), transport,
                                     lib_name + "_dealer_router",
                                     recv_timeout_ms)) {
        print_fail_no_queue();
        return;
    }

    std::vector<char> buffer(msg_size, 'a');
    queue_probe_t queue_probe(dealer.get(), router.get());

    auto print_fail_with_queue = [&]() {
        print_fail_result(
          lib_name, "DEALER_ROUTER", transport, msg_size, &queue_probe);
    };

    const int warmup_count = resolve_bench_count("PERF_WARMUP_COUNT", 1000);
    if (!run_warmup_router_echo(dealer.get(), router.get(), buffer, msg_size,
                                warmup_count)) {
        print_fail_with_queue();
        return;
    }

    latency_stats_t latency_stats;
    if (!run_latency_router_echo(dealer.get(), router.get(), buffer, msg_size,
                                 &latency_stats)) {
        print_fail_with_queue();
        return;
    }

    double throughput = 0.0;
    if (!run_throughput_router_recv(dealer.get(), router.get(), buffer,
                                    msg_size, queue_probe, &throughput)) {
        print_fail_with_queue();
        return;
    }

    const queue_stats_t queue_stats = queue_probe.snapshot();

    print_result(lib_name, "DEALER_ROUTER", transport, msg_size, throughput,
                 latency_stats.mean_us, latency_stats.p95_us,
                 latency_stats.p99_us, queue_stats);
}

int main(int argc, char** argv) {
    return run_standard_bench_main(argc, argv, run_dealer_router);
}
