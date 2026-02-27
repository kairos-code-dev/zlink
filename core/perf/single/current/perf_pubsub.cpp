#include "../common/bench_common.hpp"
#include <zlink.h>
#include <atomic>
#include <vector>
#include <cstring>

enum send_status_t
{
    send_ok = 0,
    send_would_block = 1,
    send_error = 2
};

inline send_status_t send_pub_nonblocking (void *pub_socket,
                                           const std::vector<char> &buffer,
                                           size_t msg_size)
{
    const int rc = zlink_send (
      pub_socket, buffer.data (), msg_size, ZLINK_DONTWAIT);
    if (rc >= 0)
        return send_ok;
    const int err = zlink_errno ();
    if (err == EAGAIN || err == EINTR)
        return send_would_block;
    return send_error;
}

inline int recv_sub_blocking_with_timeout (void *sub_socket,
                                           std::vector<char> &recv_buf,
                                           size_t msg_size)
{
    const int rc = zlink_recv (sub_socket, recv_buf.data (), msg_size, 0);
    if (rc >= 0)
        return 1;
    const int err = zlink_errno ();
    if (err == EAGAIN || err == EINTR)
        return 0;
    return -1;
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

    const bool is_pgm = transport == "pgm" || transport == "epgm";
    zlink_setsockopt(sub.get(), ZLINK_SUBSCRIBE, "", 0);
    int poll_timeout_ms = 0;
    if (is_pgm) {
        poll_timeout_ms =
          resolve_bench_count("PERF_PGM_POLL_TIMEOUT_MS", 50);
        const int linger_ms = 0;
        set_sockopt_int(pub.get(), ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
        set_sockopt_int(sub.get(), ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
        const char *cap = transport == "pgm" ? "pgm" : "epgm";
        if (!zlink_has(cap)) {
            print_fail_no_queue();
            return;
        }
        const int timeout_ms = poll_timeout_ms;
        set_sockopt_int(pub.get(), ZLINK_SNDTIMEO, timeout_ms, "ZLINK_SNDTIMEO");
        set_sockopt_int(sub.get(), ZLINK_RCVTIMEO, timeout_ms, "ZLINK_RCVTIMEO");
    }

    if (!setup_connected_pair(pub.get(), sub.get(), transport,
                              lib_name + "_pubsub")) {
        print_fail_no_queue();
        return;
    }
    if (!is_pgm) {
        const int recv_timeout_ms = resolve_single_pubsub_recv_timeout_ms();
        set_sockopt_int(
          sub.get(), ZLINK_RCVTIMEO, recv_timeout_ms, "ZLINK_RCVTIMEO");
    }

    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(msg_size);
    queue_probe_t queue_probe(pub.get(), sub.get());

    auto print_fail_with_queue = [&]() {
        print_fail_result(lib_name, "PUBSUB", transport, msg_size, &queue_probe);
    };

    int warmup_count = resolve_bench_count("PERF_WARMUP_COUNT", 1000);
    if (is_pgm) {
        const int max_warmup = resolve_bench_count(
          msg_size >= 65536 ? "PERF_PGM_WARMUP_COUNT_LARGE"
                            : "PERF_PGM_WARMUP_COUNT",
          msg_size >= 65536 ? 10 : 50);
        if (warmup_count > max_warmup)
            warmup_count = max_warmup;
    }
    const int throughput_duration_s = resolve_single_duration_seconds();
    const int latency_duration_s = resolve_single_latency_duration_seconds();

    if (is_pgm) {
        for (int i = 0; i < warmup_count; ++i) {
            const send_status_t send_rc =
              send_pub_nonblocking(pub.get(), buffer, msg_size);
            if (send_rc == send_error) {
                print_fail_with_queue();
                return;
            }
            if (send_rc != send_ok)
                continue;
            zlink_pollitem_t items[] = {{sub.get(), 0, ZLINK_POLLIN, 0}};
            if (zlink_poll(items, 1, poll_timeout_ms) > 0
                && (items[0].revents & ZLINK_POLLIN)) {
                zlink_recv(sub.get(), recv_buf.data(), msg_size, 0);
            }
        }

        latency_stats_builder_t latency_builder;
        const auto latency_deadline =
          std::chrono::steady_clock::now()
          + std::chrono::seconds(latency_duration_s > 0 ? latency_duration_s : 1);
        while (std::chrono::steady_clock::now() < latency_deadline) {
            stopwatch_t per_message;
            per_message.start();
            const send_status_t send_rc =
              send_pub_nonblocking(pub.get(), buffer, msg_size);
            if (send_rc == send_error) {
                print_fail_with_queue();
                return;
            }
            if (send_rc != send_ok)
                continue;
            zlink_pollitem_t items[] = {{sub.get(), 0, ZLINK_POLLIN, 0}};
            if (zlink_poll(items, 1, poll_timeout_ms) > 0
                && (items[0].revents & ZLINK_POLLIN)) {
                if (zlink_recv(sub.get(), recv_buf.data(), msg_size, 0)
                    >= 0) {
                    latency_builder.add(per_message.elapsed_ms() * 1000.0);
                }
            }
        }
        if (latency_builder.count() == 0) {
            print_fail_with_queue();
            return;
        }
        const latency_stats_t latency_stats = latency_builder.snapshot();

        const auto throughput_deadline =
          std::chrono::steady_clock::now()
          + std::chrono::seconds(
            throughput_duration_s > 0 ? throughput_duration_s : 1);
        queue_probe.force_sample_send();
        queue_probe.force_sample_recv();
        int received = 0;
        while (std::chrono::steady_clock::now() < throughput_deadline) {
            const send_status_t send_rc =
              send_pub_nonblocking(pub.get(), buffer, msg_size);
            if (send_rc == send_error) {
                print_fail_with_queue();
                return;
            }
            if (send_rc != send_ok)
                continue;
            queue_probe.sample_send_if_due();
            zlink_pollitem_t items[] = {{sub.get(), 0, ZLINK_POLLIN, 0}};
            if (zlink_poll(items, 1, poll_timeout_ms) > 0
                && (items[0].revents & ZLINK_POLLIN)) {
                if (zlink_recv(sub.get(), recv_buf.data(), msg_size, 0)
                    >= 0) {
                    ++received;
                    queue_probe.sample_recv_if_due();
                }
            }
        }
        queue_probe.force_sample_send();
        queue_probe.force_sample_recv();
        if (received <= 0) {
            print_fail_with_queue();
            return;
        }
        const double throughput = static_cast<double>(received)
                                  / static_cast<double>(throughput_duration_s);
        const queue_stats_t queue_stats = queue_probe.snapshot();
        print_result(lib_name, "PUBSUB", transport, msg_size, throughput,
                     latency_stats.mean_us, latency_stats.p95_us,
                     latency_stats.p99_us, queue_stats);
        return;
    }

    for (int i = 0; i < warmup_count; ++i) {
        const send_status_t send_rc =
          send_pub_nonblocking(pub.get(), buffer, msg_size);
        if (send_rc == send_error) {
            print_fail_with_queue();
            return;
        }
        if (send_rc != send_ok)
            continue;
        if (recv_sub_blocking_with_timeout(sub.get(), recv_buf, msg_size) < 0) {
            print_fail_with_queue();
            return;
        }
    }

    latency_stats_builder_t latency_builder;
    const auto latency_deadline =
      std::chrono::steady_clock::now()
      + std::chrono::seconds(latency_duration_s > 0 ? latency_duration_s : 1);
    while (std::chrono::steady_clock::now() < latency_deadline) {
        stopwatch_t per_message;
        per_message.start();
        const send_status_t send_rc =
          send_pub_nonblocking(pub.get(), buffer, msg_size);
        if (send_rc == send_error) {
            print_fail_with_queue();
            return;
        }
        if (send_rc != send_ok)
            continue;
        const int recv_rc =
          recv_sub_blocking_with_timeout(sub.get(), recv_buf, msg_size);
        if (recv_rc < 0) {
            print_fail_with_queue();
            return;
        }
        if (recv_rc > 0)
            latency_builder.add(per_message.elapsed_ms() * 1000.0);
    }
    if (latency_builder.count() == 0) {
        print_fail_with_queue();
        return;
    }
    const latency_stats_t latency_stats = latency_builder.snapshot();

    std::atomic<bool> sender_done(false);
    std::atomic<bool> recv_failed(false);
    std::atomic<int> recv_count(0);
    const auto throughput_deadline =
      std::chrono::steady_clock::now()
      + std::chrono::seconds(throughput_duration_s > 0 ? throughput_duration_s
                                                       : 1);
    std::thread receiver([&]() {
        queue_probe.force_sample_recv();
        while (!sender_done.load(std::memory_order_acquire)) {
            const int recv_rc =
              recv_sub_blocking_with_timeout(sub.get(), recv_buf, msg_size);
            if (recv_rc > 0) {
                recv_count.fetch_add(1, std::memory_order_release);
                queue_probe.sample_recv_if_due();
                continue;
            }
            if (recv_rc == 0) {
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
        const send_status_t send_rc =
          send_pub_nonblocking(pub.get(), buffer, msg_size);
        if (send_rc == send_error) {
            send_failed = true;
            break;
        }
        if (send_rc == send_ok)
            queue_probe.sample_send_if_due();
    }
    queue_probe.force_sample_send();
    sender_done.store(true, std::memory_order_release);
    receiver.join();
    if (send_failed || recv_failed.load(std::memory_order_acquire)) {
        print_fail_with_queue();
        return;
    }

    const int received = recv_count.load(std::memory_order_acquire);
    if (received <= 0) {
        print_fail_with_queue();
        return;
    }

    const double throughput = static_cast<double>(received)
                              / static_cast<double>(throughput_duration_s);
    const queue_stats_t queue_stats = queue_probe.snapshot();
    print_result(lib_name, "PUBSUB", transport, msg_size, throughput,
                 latency_stats.mean_us, latency_stats.p95_us,
                 latency_stats.p99_us, queue_stats);
}

int main(int argc, char** argv) {
    return run_standard_bench_main(argc, argv, run_pubsub);
}
