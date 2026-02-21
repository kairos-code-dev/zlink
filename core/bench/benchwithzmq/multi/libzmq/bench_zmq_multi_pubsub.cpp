#include "../common/bench_common.hpp"
#include "../common/bench_common_multi.hpp"
#include <zmq.h>
#include <vector>
#include <cerrno>
#include <algorithm>

namespace {

multi_send_result_t send_pub_blocking (void *pub,
                                       const std::vector<char> &buffer)
{
    if (zmq_send (pub, buffer.data (), buffer.size (), 0) >= 0)
        return multi_send_ok;
    const int err = zmq_errno ();
    if (err == EAGAIN || err == EINTR)
        return multi_send_would_block;
    if (err == ETERM || err == ENOTSOCK)
        return multi_send_error;
    return multi_send_error;
}

int recv_batch_subscribers (const std::vector<void *> &subs,
                            std::vector<zmq_pollitem_t> &poll_items,
                            std::vector<char> &recv_buf,
                            int recv_batch,
                            long poll_timeout_ms)
{
    if (subs.empty ())
        return 0;

    for (size_t i = 0; i < poll_items.size (); ++i)
        poll_items[i].revents = 0;

    const int prc = zmq_poll (&poll_items[0], poll_items.size (), poll_timeout_ms);
    if (prc < 0)
        return zmq_errno () == EINTR ? 0 : -1;
    if (prc == 0)
        return 0;

    int received = 0;
    for (size_t i = 0; i < subs.size () && received < recv_batch; ++i) {
        if ((poll_items[i].revents & ZMQ_POLLIN) == 0)
            continue;
        if (zmq_recv (subs[i], recv_buf.data (), recv_buf.size (), 0) < 0)
            return -1;
        ++received;
    }

    while (received < recv_batch) {
        bool got_any = false;
        for (size_t i = 0; i < subs.size () && received < recv_batch; ++i) {
            const int rc =
              zmq_recv (subs[i], recv_buf.data (), recv_buf.size (), ZMQ_DONTWAIT);
            if (rc >= 0) {
                ++received;
                got_any = true;
                continue;
            }
            if (zmq_errno () != EAGAIN && zmq_errno () != EINTR)
                return -1;
        }
        if (!got_any)
            break;
    }

    return received;
}

void drain_subscribers_queues (const std::vector<void *> &subs,
                               std::vector<char> &recv_buf,
                               int drain_ms)
{
    if (subs.empty ())
        return;

    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (std::max (0, drain_ms));
    while (std::chrono::steady_clock::now () < deadline) {
        bool got_any = false;
        for (size_t i = 0; i < subs.size (); ++i) {
            while (true) {
                const int rc = zmq_recv (
                  subs[i], recv_buf.data (), recv_buf.size (), ZMQ_DONTWAIT);
                if (rc >= 0) {
                    got_any = true;
                    continue;
                }
                const int err = zmq_errno ();
                if (err == EAGAIN || err == EINTR)
                    break;
                return;
            }
        }
        if (!got_any)
            break;
    }
}

struct pubsub_measure_result_t
{
    long measure_recv;
    bool failed;

    pubsub_measure_result_t () : measure_recv (0), failed (false) {}
};

pubsub_measure_result_t run_pubsub_measure (void *pub,
                                            const std::vector<void *> &subs,
                                            const multi_bench_settings_t &settings,
                                            std::vector<char> &buffer,
                                            std::vector<char> &recv_buf,
                                            long poll_timeout_ms)
{
    pubsub_measure_result_t out;
    if (!pub || subs.empty ()) {
        out.failed = true;
        return out;
    }

    std::vector<zmq_pollitem_t> poll_items;
    poll_items.reserve (subs.size ());
    for (size_t i = 0; i < subs.size (); ++i) {
        zmq_pollitem_t item = {subs[i], 0, ZMQ_POLLIN, 0};
        poll_items.push_back (item);
    }

    std::atomic<int> phase (0); // 0=settle, 1=measure, 2=drain, 3=stop
    std::atomic<bool> fatal (false);
    std::atomic<long> measure_recv (0);

    std::thread receiver ([&] () {
        while (!fatal.load (std::memory_order_acquire)) {
            const int cur = phase.load (std::memory_order_acquire);
            if (cur == 3)
                break;

            const int count = recv_batch_subscribers (
              subs, poll_items, recv_buf, settings.recv_batch, poll_timeout_ms);
            if (count < 0) {
                fatal.store (true, std::memory_order_release);
                break;
            }
            if (count == 0) {
                std::this_thread::sleep_for (std::chrono::microseconds (50));
                continue;
            }

            if (cur == 1)
                measure_recv.fetch_add (count, std::memory_order_relaxed);
        }
    });

    if (settings.settle_ms > 0)
        std::this_thread::sleep_for (std::chrono::milliseconds (settings.settle_ms));

    phase.store (1, std::memory_order_release);

    const auto measure_end =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (std::max (1, settings.measure_seconds));

    while (std::chrono::steady_clock::now () < measure_end
           && !fatal.load (std::memory_order_acquire)) {
        const multi_send_result_t rc = send_pub_blocking (pub, buffer);
        if (rc == multi_send_ok)
            continue;
        if (rc == multi_send_error) {
            fatal.store (true, std::memory_order_release);
            break;
        }

        if (settings.send_backoff_us > 0)
            std::this_thread::sleep_for (
              std::chrono::microseconds (settings.send_backoff_us));
        else
            std::this_thread::yield ();
    }

    phase.store (2, std::memory_order_release);
    const auto drain_deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (std::max (0, settings.drain_ms));
    while (std::chrono::steady_clock::now () < drain_deadline
           && !fatal.load (std::memory_order_acquire)) {
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    phase.store (3, std::memory_order_release);
    if (receiver.joinable ())
        receiver.join ();

    out.measure_recv = measure_recv.load (std::memory_order_relaxed);
    out.failed = fatal.load (std::memory_order_acquire);
    return out;
}

double measure_pubsub_latency_us (void *ctx,
                                  const std::string &transport,
                                  const std::string &lib_name,
                                  size_t msg_size,
                                  int hwm,
                                  int ready_timeout_ms,
                                  int poll_timeout_ms)
{
    void *pub = zmq_socket (ctx, ZMQ_PUB);
    void *sub = zmq_socket (ctx, ZMQ_SUB);
    if (!pub || !sub) {
        if (sub)
            zmq_close (sub);
        if (pub)
            zmq_close (pub);
        return 0.0;
    }

    const int linger_ms = 0;
    set_sockopt_int (pub, ZMQ_LINGER, linger_ms, "ZMQ_LINGER");
    set_sockopt_int (sub, ZMQ_LINGER, linger_ms, "ZMQ_LINGER");
    apply_benchmark_hwm (pub, hwm);
    apply_benchmark_hwm (sub, hwm);
    zmq_setsockopt (sub, ZMQ_SUBSCRIBE, "", 0);

    const std::string endpoint =
      bind_and_resolve_endpoint (pub, transport, lib_name + "_multi_pubsub_lat");
    if (endpoint.empty ()) {
        zmq_close (sub);
        zmq_close (pub);
        return 0.0;
    }

    connect_monitor_t monitor;
    if (!open_connect_monitor (ctx, sub, lib_name + "_multi_pubsub_lat", 0, monitor)) {
        zmq_close (sub);
        zmq_close (pub);
        return 0.0;
    }

    if (!connect_checked (sub, endpoint)
        || !wait_connect_ready (monitor, ready_timeout_ms)) {
        close_connect_monitor (monitor);
        zmq_close (sub);
        zmq_close (pub);
        return 0.0;
    }

    close_connect_monitor (monitor);
    settle ();

    std::vector<char> buffer (std::max<size_t> (1, msg_size), 'a');
    std::vector<char> recv_buf (std::max<size_t> (1, msg_size));

    const int lat_count = resolve_bench_count ("BENCH_LAT_COUNT", 500);
    int received = 0;
    auto start = std::chrono::steady_clock::now ();
    for (int i = 0; i < lat_count; ++i) {
        if (zmq_send (pub, buffer.data (), buffer.size (), 0) < 0)
            continue;

        zmq_pollitem_t item[] = {{sub, 0, ZMQ_POLLIN, 0}};
        if (zmq_poll (item, 1, poll_timeout_ms) <= 0
            || (item[0].revents & ZMQ_POLLIN) == 0)
            continue;

        if (zmq_recv (sub, recv_buf.data (), recv_buf.size (), 0) >= 0)
            ++received;
    }

    double latency = 0.0;
    if (received > 0) {
        const auto elapsed = std::chrono::steady_clock::now () - start;
        latency =
          (std::chrono::duration<double, std::milli> (elapsed).count () * 1000.0)
          / static_cast<double> (received);
    }

    zmq_close (sub);
    zmq_close (pub);
    return latency;
}

} // namespace

void run_multi_pubsub (const std::string &transport,
                       size_t msg_size,
                       int /*msg_count*/,
                       const std::string &lib_name)
{
    if (!transport_available (transport))
        return;

    const multi_bench_settings_t settings = resolve_multi_bench_settings ();
    if (settings.clients == 0) {
        print_result (lib_name, "MULTI_PUBSUB", transport, msg_size, 0.0, 0.0);
        return;
    }

    ctx_guard_t ctx;
    if (!ctx.valid ())
        return;

    void *pub = zmq_socket (ctx.get (), ZMQ_PUB);
    if (!pub)
        return;

    const int linger_ms = 0;
    const int sndbuf = resolve_bench_count ("BENCH_MULTI_SNDBUF", 8388608);
    const int rcvbuf = resolve_bench_count ("BENCH_MULTI_RCVBUF", 8388608);
    const int sndtimeo_ms =
      resolve_bench_count ("BENCH_MULTI_SNDTIMEO_MS", 5000);
    set_sockopt_int (pub, ZMQ_LINGER, linger_ms, "ZMQ_LINGER");
    set_sockopt_int (pub, ZMQ_SNDBUF, sndbuf, "ZMQ_SNDBUF");
    set_sockopt_int (pub, ZMQ_SNDTIMEO, sndtimeo_ms, "ZMQ_SNDTIMEO");
    apply_benchmark_hwm (pub, settings.hwm);

    std::vector<void *> subs (settings.clients, NULL);
    for (size_t i = 0; i < subs.size (); ++i) {
        subs[i] = zmq_socket (ctx.get (), ZMQ_SUB);
        if (!subs[i]) {
            for (size_t j = 0; j < i; ++j)
                zmq_close (subs[j]);
            zmq_close (pub);
            return;
        }
        zmq_setsockopt (subs[i], ZMQ_SUBSCRIBE, "", 0);
        set_sockopt_int (subs[i], ZMQ_LINGER, linger_ms, "ZMQ_LINGER");
        set_sockopt_int (subs[i], ZMQ_RCVBUF, rcvbuf, "ZMQ_RCVBUF");
        apply_benchmark_hwm (subs[i], settings.hwm);
    }

    connect_monitor_t server_monitor;
    if (!open_connect_monitor (ctx.get (), pub, lib_name + "_multi_pubsub_srv", 0,
                               server_monitor)) {
        for (void *sock : subs) {
            if (sock)
                zmq_close (sock);
        }
        zmq_close (pub);
        return;
    }

    auto cleanup = [&] () {
        close_connect_monitor (server_monitor);
        for (void *sock : subs) {
            if (sock)
                zmq_close (sock);
        }
        zmq_close (pub);
    };

    const bool is_pgm = transport == "pgm" || transport == "epgm";
    int poll_timeout_ms = 50;
    if (is_pgm) {
        const char *cap = transport == "pgm" ? "pgm" : "epgm";
        if (!zmq_has (cap)) {
            print_result (
              lib_name, "MULTI_PUBSUB", transport, msg_size, 0.0, 0.0);
            cleanup ();
            return;
        }

        poll_timeout_ms = resolve_bench_count ("BENCH_PGM_POLL_TIMEOUT_MS", 50);
        set_sockopt_int (pub, ZMQ_SNDTIMEO, poll_timeout_ms, "ZMQ_SNDTIMEO");
        for (void *sub : subs) {
            set_sockopt_int (sub, ZMQ_RCVTIMEO, poll_timeout_ms, "ZMQ_RCVTIMEO");
        }
    } else {
        poll_timeout_ms = resolve_bench_count ("BENCH_PUBSUB_POLL_TIMEOUT_MS", 50);
    }
    const int measure_poll_timeout_ms =
      resolve_bench_count ("BENCH_MULTI_PUBSUB_MEASURE_POLL_TIMEOUT_MS", 0);

    double connect_prep_ms = 0.0;
    double ready_wait_ms = 0.0;
    const std::string endpoint =
      bind_and_resolve_endpoint (pub, transport, lib_name + "_multi_pubsub");
    const auto connect_start = std::chrono::steady_clock::now ();
    if (endpoint.empty ()
        || !connect_clients_concurrently (
          subs,
          endpoint,
          [] (void *sock, const std::string &ep) {
              return connect_checked (sock, ep);
          })) {
        cleanup ();
        return;
    }
    const auto connect_end = std::chrono::steady_clock::now ();
    connect_prep_ms = std::chrono::duration<double, std::milli> (
                        connect_end - connect_start)
                        .count ();

    const auto ready_start = std::chrono::steady_clock::now ();
    const bool ready_ok = wait_connect_ready_count (
      server_monitor, settings.clients, settings.connect_ready_timeout_ms);
    const auto ready_end = std::chrono::steady_clock::now ();
    ready_wait_ms = std::chrono::duration<double, std::milli> (
                      ready_end - ready_start)
                      .count ();
    if (!ready_ok) {
        cleanup ();
        return;
    }

    settle ();

    const std::vector<size_t> msg_sizes = resolve_bench_msg_sizes (msg_size);
    for (size_t s = 0; s < msg_sizes.size (); ++s) {
        const size_t current_size = msg_sizes[s];
        std::vector<char> buffer (std::max<size_t> (1, current_size), 'a');
        std::vector<char> recv_buf (std::max<size_t> (1, current_size));

        const pubsub_measure_result_t bench = run_pubsub_measure (
          pub, subs, settings, buffer, recv_buf, measure_poll_timeout_ms);

        drain_subscribers_queues (
          subs, recv_buf, std::max (settings.drain_ms, 2000));

        double latency = 0.0;
        ctx_guard_t lat_ctx;
        if (lat_ctx.valid ()) {
            latency = measure_pubsub_latency_us (
              lat_ctx.get (),
              transport,
              lib_name,
              current_size,
              settings.hwm,
              settings.connect_ready_timeout_ms,
              poll_timeout_ms);
        }

        const double throughput =
          !bench.failed && bench.measure_recv > 0
            ? static_cast<double> (bench.measure_recv)
                / static_cast<double> (std::max (1, settings.measure_seconds))
            : 0.0;

        const double prep_connect_ms = s == 0 ? connect_prep_ms : 0.0;
        const double prep_ready_ms = s == 0 ? ready_wait_ms : 0.0;
        print_prep_result (lib_name, "MULTI_PUBSUB", transport, current_size,
                           prep_connect_ms, prep_ready_ms);
        print_result (lib_name, "MULTI_PUBSUB", transport, current_size,
                      throughput, latency);
    }
    cleanup ();
}

int main (int argc, char **argv)
{
    return run_standard_bench_main (argc, argv, run_multi_pubsub);
}
