#include "../common/bench_common.hpp"
#include <zlink.h>
#include <atomic>
#include <thread>
#include <vector>
#include <cstring>

namespace {

bool wait_for_input(zlink_pollitem_t *item, long timeout_ms)
{
    const int rc = zlink_poll(item, 1, timeout_ms);
    if (rc <= 0)
        return false;
    return (item[0].revents & ZLINK_POLLIN) != 0;
}

bool perform_handshake_poll(void *router1, void *router2)
{
    zlink_pollitem_t poll_r1[] = {{router1, 0, ZLINK_POLLIN, 0}};
    zlink_pollitem_t poll_r2[] = {{router2, 0, ZLINK_POLLIN, 0}};

    bool connected = false;
    char buf[16];
    int handshake_attempts = 0;
    while (!connected) {
        ++handshake_attempts;
        zlink_send(router2, "ROUTER1", 7, ZLINK_SNDMORE | ZLINK_DONTWAIT);
        const int rc = zlink_send(router2, "PING", 4, ZLINK_DONTWAIT);

        if (rc == 4 && wait_for_input(poll_r1, 0)) {
            const int id_len = zlink_recv(router1, buf, sizeof(buf), ZLINK_DONTWAIT);
            if (id_len > 0 && recv_exact(router1, buf, 4, 0))
                connected = true;
        }

        if (connected)
            break;
        if (handshake_attempts > 100)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!send_exact(router1, "ROUTER2", 7, ZLINK_SNDMORE)
        || !send_exact(router1, "PONG", 4, 0)) {
        return false;
    }

    if (!wait_for_input(poll_r2, -1))
        return false;
    if (zlink_recv(router2, buf, sizeof(buf), 0) <= 0
        || !recv_exact(router2, buf, 4, 0)) {
        return false;
    }

    return drain_socket_nonblocking(router1)
           && drain_socket_nonblocking(router2);
}

bool run_latency_poll(void *router1,
                      void *router2,
                      const std::vector<char> &buffer,
                      std::vector<char> &recv_buf,
                      size_t msg_size,
                      int recv_timeout_ms,
                      latency_stats_t *out_stats)
{
    if (!out_stats)
        return false;

    zlink_pollitem_t poll_r1[] = {{router1, 0, ZLINK_POLLIN, 0}};
    zlink_pollitem_t poll_r2[] = {{router2, 0, ZLINK_POLLIN, 0}};
    char id[256];

    const int latency_duration_s = resolve_single_latency_duration_seconds();
    latency_stats_builder_t latency_builder;
    const auto latency_deadline =
      std::chrono::steady_clock::now()
      + std::chrono::seconds(latency_duration_s > 0 ? latency_duration_s : 1);
    while (std::chrono::steady_clock::now() < latency_deadline) {
        stopwatch_t sw;
        sw.start();
        if (!send_exact(router2, "ROUTER1", 7, ZLINK_SNDMORE)
            || !send_exact(router2, buffer.data(), msg_size, 0)) {
            return false;
        }

        if (!wait_for_input(poll_r1, recv_timeout_ms))
            return false;
        const int id_len = zlink_recv(router1, id, sizeof(id), 0);
        if (id_len <= 0
            || !recv_exact(router1, recv_buf.data(), msg_size, 0)
            || !send_exact(router1, id, static_cast<size_t>(id_len), ZLINK_SNDMORE)
            || !send_exact(router1, buffer.data(), msg_size, 0)) {
            return false;
        }

        if (!wait_for_input(poll_r2, recv_timeout_ms))
            return false;
        if (zlink_recv(router2, id, sizeof(id), 0) <= 0
            || !recv_exact(router2, recv_buf.data(), msg_size, 0)) {
            return false;
        }

        latency_builder.add((sw.elapsed_ms() * 1000.0) * 0.5);
    }

    if (latency_builder.count() == 0)
        return false;

    *out_stats = latency_builder.snapshot();
    return true;
}

bool run_throughput_poll(void *router1,
                         void *router2,
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
        zlink_pollitem_t poll_r1[] = {{router1, 0, ZLINK_POLLIN, 0}};
        std::vector<char> recv_buf(msg_size + 256);
        char id[256];
        auto account_recv = [&]() {
            recv_total.fetch_add(1, std::memory_order_release);
            if (std::chrono::steady_clock::now() < throughput_deadline)
                recv_in_window.fetch_add(1, std::memory_order_release);
            queue_probe.sample_recv_if_due();
        };
        queue_probe.force_sample_recv();
        while (!sender_done.load(std::memory_order_acquire)
               || recv_total.load(std::memory_order_acquire)
                    < sent_count.load(std::memory_order_acquire)) {
            if (!wait_for_input(poll_r1, 0)) {
                std::this_thread::yield();
                continue;
            }

            const int id_len = zlink_recv(router1, id, sizeof(id), 0);
            if (id_len <= 0) {
                const int err = zlink_errno();
                if (err == EAGAIN || err == EINTR)
                    continue;
                recv_failed.store(true, std::memory_order_release);
                break;
            }
            if (!recv_exact(router1, recv_buf.data(), msg_size, 0)) {
                recv_failed.store(true, std::memory_order_release);
                break;
            }
            account_recv();

            // Drain immediately available messages in a non-blocking burst.
            for (;;) {
                const int burst_id_len =
                  zlink_recv(router1, id, sizeof(id), ZLINK_DONTWAIT);
                if (burst_id_len > 0) {
                    if (!recv_exact(router1, recv_buf.data(), msg_size, 0)) {
                        recv_failed.store(true, std::memory_order_release);
                        break;
                    }
                    account_recv();
                    continue;
                }

                const int burst_err = zlink_errno();
                if (burst_err == EAGAIN || burst_err == EINTR)
                    break;

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
        if (!send_exact(router2, "ROUTER1", 7, ZLINK_SNDMORE)
            || !send_exact(router2, buffer.data(), msg_size, 0)) {
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

bool setup_router_router_poll_session(void *router1,
                                      void *router2,
                                      const std::string &transport,
                                      const std::string &pair_id,
                                      int recv_timeout_ms)
{
    if (!router1 || !router2)
        return false;

    zlink_setsockopt(router1, ZLINK_ROUTING_ID, "ROUTER1", 7);
    zlink_setsockopt(router2, ZLINK_ROUTING_ID, "ROUTER2", 7);

    int mandatory = 1;
    zlink_setsockopt(router1, ZLINK_ROUTER_MANDATORY, &mandatory,
                     sizeof(mandatory));
    zlink_setsockopt(router2, ZLINK_ROUTER_MANDATORY, &mandatory,
                     sizeof(mandatory));

    if (!setup_connected_pair(router1, router2, transport, pair_id)
        || !perform_handshake_poll(router1, router2)) {
        return false;
    }

    set_sockopt_int(router1, ZLINK_RCVTIMEO, recv_timeout_ms,
                    "ZLINK_RCVTIMEO");
    set_sockopt_int(router2, ZLINK_RCVTIMEO, recv_timeout_ms,
                    "ZLINK_RCVTIMEO");
    return true;
}

} // namespace

void run_router_router_poll(const std::string &transport,
                            size_t msg_size,
                            const std::string &lib_name)
{
    if (!transport_available(transport))
        return;

    auto print_fail_no_queue = [&]() {
        print_fail_result(
          lib_name, "ROUTER_ROUTER_POLL", transport, msg_size);
    };

    ctx_guard_t ctx;
    if (!ctx.valid()) {
        print_fail_no_queue();
        return;
    }

    socket_guard_t router1(ctx.get(), ZLINK_ROUTER);
    socket_guard_t router2(ctx.get(), ZLINK_ROUTER);
    if (!router1.valid() || !router2.valid()) {
        print_fail_no_queue();
        return;
    }

    const int recv_timeout_ms = resolve_single_recv_timeout_ms();
    if (!setup_router_router_poll_session(
          router1.get(), router2.get(), transport,
          lib_name + "_router_router_poll", recv_timeout_ms)) {
        print_fail_no_queue();
        return;
    }

    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(msg_size + 256);
    queue_probe_t queue_probe(router2.get(), router1.get());

    auto print_fail_with_queue = [&]() {
        print_fail_result(
          lib_name, "ROUTER_ROUTER_POLL", transport, msg_size, &queue_probe);
    };

    latency_stats_t latency_stats;
    if (!run_latency_poll(router1.get(), router2.get(), buffer, recv_buf,
                          msg_size, recv_timeout_ms, &latency_stats)) {
        print_fail_with_queue();
        return;
    }

    double throughput = 0.0;
    if (!run_throughput_poll(router1.get(), router2.get(), buffer, msg_size,
                             queue_probe, &throughput)) {
        print_fail_with_queue();
        return;
    }

    const queue_stats_t queue_stats = queue_probe.snapshot();
    print_result(lib_name, "ROUTER_ROUTER_POLL", transport, msg_size,
                 throughput, latency_stats.mean_us, latency_stats.p95_us,
                 latency_stats.p99_us, queue_stats);
}

int main(int argc, char **argv)
{
    return run_standard_bench_main(argc, argv, run_router_router_poll);
}
