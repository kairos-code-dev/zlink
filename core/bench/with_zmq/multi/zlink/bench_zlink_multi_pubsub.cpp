#include "../common/bench_common_zlink.hpp"
#include "../common/bench_common_multi.hpp"
#include <zlink.h>
#include <vector>
#include <cerrno>
#include <algorithm>
#include <chrono>
#include <cstring>

namespace {

inline unsigned long long wallclock_now_us ()
{
    const auto now =
      std::chrono::time_point_cast<std::chrono::microseconds> (
        std::chrono::system_clock::now ());
    return static_cast<unsigned long long> (now.time_since_epoch ().count ());
}

multi_send_result_t send_pub_blocking (void *pub,
                                       const std::vector<char> &buffer)
{
    zlink_msg_t msg;
    if (zlink_msg_init_size (&msg, buffer.size ()) != 0)
        return multi_send_error;
    if (!buffer.empty ())
        std::memcpy (zlink_msg_data (&msg), &buffer[0], buffer.size ());
    if (zlink_msg_send (&msg, pub, 0) >= 0)
        return multi_send_ok;
    const int err = zlink_errno ();
    zlink_msg_close (&msg);
    if (err == EAGAIN || err == EINTR)
        return multi_send_would_block;
    if (err == ETERM || err == ENOTSOCK)
        return multi_send_error;
    return multi_send_error;
}

int recv_sub_message (void *sub,
                      std::vector<char> &recv_buf,
                      zlink_send_flags_t flags_)
{
    zlink_msg_t msg;
    zlink_msg_init (&msg);
    const int rc = zlink_msg_recv (&msg, sub, flags_);
    if (rc < 0) {
        zlink_msg_close (&msg);
        return -1;
    }

    const size_t copy_size =
      std::min (recv_buf.size (), zlink_msg_size (&msg));
    if (copy_size > 0)
        std::memcpy (&recv_buf[0], zlink_msg_data (&msg), copy_size);
    zlink_msg_close (&msg);
    return rc;
}

int recv_batch_subscribers (const std::vector<void *> &subs,
                            std::vector<zlink_pollitem_t> &poll_items,
                            std::vector<char> &recv_buf,
                            size_t &rr_cursor,
                            int recv_batch,
                            long poll_timeout_ms)
{
    if (subs.empty ())
        return 0;

    for (size_t i = 0; i < poll_items.size (); ++i)
        poll_items[i].revents = 0;

    const int prc =
      zlink_poll (&poll_items[0], poll_items.size (), poll_timeout_ms);
    if (prc < 0)
        return zlink_errno () == EINTR ? 0 : -1;
    if (prc == 0)
        return 0;

    int received = 0;
    const size_t sub_count = subs.size ();
    for (size_t i = 0; i < sub_count && received < recv_batch; ++i) {
        const size_t idx = (rr_cursor + i) % sub_count;
        if ((poll_items[idx].revents & ZLINK_POLLIN) == 0)
            continue;
        if (recv_sub_message (subs[idx], recv_buf, ZLINK_DONTWAIT) < 0) {
            const int err = zlink_errno ();
            if (err == EAGAIN || err == EINTR)
                continue;
            return -1;
        }
        rr_cursor = (idx + 1) % sub_count;
        ++received;
    }

    while (received < recv_batch) {
        bool got_any = false;
        for (size_t i = 0; i < sub_count && received < recv_batch; ++i) {
            const size_t idx = (rr_cursor + i) % sub_count;
            const int rc =
              recv_sub_message (subs[idx], recv_buf, ZLINK_DONTWAIT);
            if (rc >= 0) {
                ++received;
                got_any = true;
                rr_cursor = (idx + 1) % sub_count;
                continue;
            }
            if (zlink_errno () != EAGAIN && zlink_errno () != EINTR)
                return -1;
        }
        if (!got_any)
            break;
    }

    return received;
}

int recv_available_subscribers (const std::vector<void *> &subs,
                                std::vector<zlink_pollitem_t> &poll_items,
                                std::vector<char> &recv_buf,
                                size_t &rr_cursor,
                                int recv_batch,
                                long poll_timeout_ms)
{
    int total = 0;
    long timeout = poll_timeout_ms;
    while (true) {
        const int count = recv_batch_subscribers (
          subs, poll_items, recv_buf, rr_cursor, recv_batch, timeout);
        if (count < 0)
            return -1;
        if (count == 0)
            return total;
        total += count;
        if (count < recv_batch)
            return total;
        timeout = 0;
    }
}

void drain_subscribers_queues (const std::vector<void *> &subs,
                               std::vector<char> &recv_buf,
                               size_t &rr_cursor,
                               int drain_ms)
{
    if (subs.empty ())
        return;

    const size_t sub_count = subs.size ();
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (std::max (0, drain_ms));
    while (std::chrono::steady_clock::now () < deadline) {
        bool got_any = false;
        for (size_t i = 0; i < sub_count; ++i) {
            const size_t idx = (rr_cursor + i) % sub_count;
            while (true) {
                const int rc =
                  recv_sub_message (subs[idx], recv_buf, ZLINK_DONTWAIT);
                if (rc >= 0) {
                    got_any = true;
                    rr_cursor = (idx + 1) % sub_count;
                    continue;
                }
                const int err = zlink_errno ();
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

bool run_pubsub_warmup (void *pub,
                        const std::vector<void *> &subs,
                        const multi_bench_settings_t &settings,
                        const std::vector<char> &buffer,
                        std::vector<char> &recv_buf,
                        long poll_timeout_ms)
{
    if (!pub || subs.empty ())
        return false;

    const int warmup_seconds =
      resolve_bench_count ("BENCH_MULTI_WARMUP_SECONDS", 1);
    if (warmup_seconds <= 0)
        return true;

    std::vector<zlink_pollitem_t> poll_items;
    poll_items.reserve (subs.size ());
    for (size_t i = 0; i < subs.size (); ++i) {
        zlink_pollitem_t item = {subs[i], 0, ZLINK_POLLIN, 0};
        poll_items.push_back (item);
    }
    size_t rr_cursor = 0;
    const int recv_batch =
      std::max (settings.recv_batch, static_cast<int> (subs.size ()));
    const int target_drain = std::max (1, static_cast<int> (subs.size ()));

    const auto warmup_end = std::chrono::steady_clock::now ()
                            + std::chrono::seconds (warmup_seconds);
    while (std::chrono::steady_clock::now () < warmup_end) {
        bool progressed = false;
        const multi_send_result_t rc = send_pub_blocking (pub, buffer);
        if (rc == multi_send_error)
            return false;
        if (rc == multi_send_ok)
            progressed = true;

        int drained = 0;
        long timeout = poll_timeout_ms;
        while (drained < target_drain) {
            const int count = recv_available_subscribers (
              subs, poll_items, recv_buf, rr_cursor, recv_batch, timeout);
            if (count < 0)
                return false;
            if (count == 0)
                break;
            progressed = true;
            drained += count;
            timeout = 0;
        }

        if (!progressed)
            std::this_thread::yield ();
    }

    return true;
}

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

    std::vector<zlink_pollitem_t> poll_items;
    poll_items.reserve (subs.size ());
    for (size_t i = 0; i < subs.size (); ++i) {
        zlink_pollitem_t item = {subs[i], 0, ZLINK_POLLIN, 0};
        poll_items.push_back (item);
    }
    size_t rr_cursor = 0;
    const int recv_batch =
      std::max (settings.recv_batch, static_cast<int> (subs.size ()));
    const int target_drain = std::max (1, static_cast<int> (subs.size ()));

    long measure_recv = 0;
    const auto measure_end =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (std::max (1, settings.duration_seconds));

    while (std::chrono::steady_clock::now () < measure_end) {
        const multi_send_result_t rc = send_pub_blocking (pub, buffer);
        if (rc == multi_send_error) {
            out.failed = true;
            break;
        }

        int drained = 0;
        long timeout = poll_timeout_ms;
        while (drained < target_drain) {
            const int count = recv_available_subscribers (
              subs, poll_items, recv_buf, rr_cursor, recv_batch, timeout);
            if (count < 0) {
                out.failed = true;
                break;
            }
            if (count == 0)
                break;
            measure_recv += count;
            drained += count;
            timeout = 0;
        }
        if (out.failed)
            break;
    }

    out.measure_recv = measure_recv;
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
    void *pub = zlink_socket (ctx, ZLINK_SOCKET_PUB);
    void *sub = zlink_socket (ctx, ZLINK_SOCKET_SUB);
    if (!pub || !sub) {
        if (sub)
            zlink_close (sub);
        if (pub)
            zlink_close (pub);
        return 0.0;
    }

    const int linger_ms = 0;
    set_sockopt_int (pub, ZLINK_OPT_LINGER, linger_ms, "ZLINK_OPT_LINGER");
    set_sockopt_int (sub, ZLINK_OPT_LINGER, linger_ms, "ZLINK_OPT_LINGER");
    apply_benchmark_hwm (pub, hwm);
    apply_benchmark_hwm (sub, hwm);
    zlink_set_subscription (sub, "");

    const std::string endpoint =
      bind_and_resolve_endpoint (pub, transport, lib_name + "_multi_pubsub_lat");
    if (endpoint.empty ()) {
        zlink_close (sub);
        zlink_close (pub);
        return 0.0;
    }

    connect_monitor_t monitor;
    if (!open_connect_monitor (sub, monitor)) {
        zlink_close (sub);
        zlink_close (pub);
        return 0.0;
    }

    if (!connect_checked (sub, endpoint)
        || !wait_connect_ready (monitor, ready_timeout_ms)) {
        close_connect_monitor (monitor);
        zlink_close (sub);
        zlink_close (pub);
        return 0.0;
    }

    close_connect_monitor (monitor);
    settle ();

    std::vector<char> buffer (std::max<size_t> (1, msg_size), 'a');
    std::vector<char> recv_buf (std::max<size_t> (1, msg_size));

    const int lat_count = resolve_bench_count ("BENCH_LAT_COUNT", 500);
    int received = 0;
    double lat_sum_us = 0.0;
    for (int i = 0; i < lat_count; ++i) {
        while (recv_sub_message (sub, recv_buf, ZLINK_DONTWAIT) >= 0) {
        }
        if (zlink_errno () != EAGAIN && zlink_errno () != EINTR)
            continue;

        const unsigned long long send_ts_us = wallclock_now_us ();
        if (buffer.size () >= sizeof (send_ts_us))
            std::memcpy (buffer.data (), &send_ts_us, sizeof (send_ts_us));

        zlink_msg_t send_msg;
        if (zlink_msg_init_size (&send_msg, buffer.size ()) != 0)
            continue;
        if (!buffer.empty ())
            std::memcpy (zlink_msg_data (&send_msg), &buffer[0], buffer.size ());
        if (zlink_msg_send (&send_msg, pub, 0) < 0) {
            zlink_msg_close (&send_msg);
            continue;
        }

        const auto deadline = std::chrono::steady_clock::now ()
                              + std::chrono::milliseconds (
                                std::max (1, poll_timeout_ms));
        while (std::chrono::steady_clock::now () < deadline) {
            const auto remain_ms = static_cast<long> (
              std::chrono::duration_cast<std::chrono::milliseconds> (
                deadline - std::chrono::steady_clock::now ())
                .count ());
            if (remain_ms <= 0)
                break;

            zlink_pollitem_t item[] = {{sub, 0, ZLINK_POLLIN, 0}};
            if (zlink_poll (item, 1, remain_ms) <= 0
                || (item[0].revents & ZLINK_POLLIN) == 0)
                continue;

            const int rc = recv_sub_message (sub, recv_buf, 0);
            if (rc < 0)
                continue;

            unsigned long long observed_send_ts_us = send_ts_us;
            if (static_cast<size_t> (rc) >= sizeof (observed_send_ts_us)) {
                std::memcpy (
                  &observed_send_ts_us,
                  recv_buf.data (),
                  sizeof (observed_send_ts_us));
            }
            if (observed_send_ts_us != send_ts_us)
                continue;

            const unsigned long long now_us = wallclock_now_us ();
            if (now_us >= observed_send_ts_us) {
                lat_sum_us +=
                  static_cast<double> (now_us - observed_send_ts_us);
                ++received;
            }
            break;
        }
    }

    double latency = 0.0;
    if (received > 0)
        latency = lat_sum_us / static_cast<double> (received);

    zlink_close (sub);
    zlink_close (pub);
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

    void *pub = zlink_socket (ctx.get (), ZLINK_SOCKET_PUB);
    if (!pub)
        return;

    const int linger_ms = 0;
    const int pubsub_hwm = resolve_bench_count ("BENCH_MULTI_PUBSUB_HWM", 5000);
    const int sndtimeo_ms = resolve_bench_count ("BENCH_MULTI_SNDTIMEO_MS", 5000);
    const int rcvtimeo_ms = resolve_bench_count ("BENCH_MULTI_RCVTIMEO_MS", 5000);
    set_sockopt_int (pub, ZLINK_OPT_LINGER, linger_ms, "ZLINK_OPT_LINGER");
    set_sockopt_int (pub, ZLINK_OPT_SNDTIMEO, sndtimeo_ms,
                     "ZLINK_OPT_SNDTIMEO");
    apply_benchmark_hwm (pub, pubsub_hwm);

    std::vector<void *> subs (settings.clients, NULL);
    for (size_t i = 0; i < subs.size (); ++i) {
        subs[i] = zlink_socket (ctx.get (), ZLINK_SOCKET_SUB);
        if (!subs[i]) {
            for (size_t j = 0; j < i; ++j)
                zlink_close (subs[j]);
            zlink_close (pub);
            return;
        }
        zlink_set_subscription (subs[i], "");
        set_sockopt_int (subs[i], ZLINK_OPT_LINGER, linger_ms,
                         "ZLINK_OPT_LINGER");
        set_sockopt_int (subs[i], ZLINK_OPT_RCVTIMEO, rcvtimeo_ms,
                         "ZLINK_OPT_RCVTIMEO");
        apply_benchmark_hwm (subs[i], pubsub_hwm);
    }

    connect_monitor_t server_monitor;
    if (!open_connect_monitor (pub, server_monitor)) {
        for (void *sock : subs) {
            if (sock)
                zlink_close (sock);
        }
        zlink_close (pub);
        return;
    }

    auto cleanup = [&] () {
        close_connect_monitor (server_monitor);
        for (void *sock : subs) {
            if (sock)
                zlink_close (sock);
        }
        zlink_close (pub);
    };

    const bool is_pgm = transport == "pgm" || transport == "epgm";
    int poll_timeout_ms = 50;
    if (is_pgm) {
        const char *cap = transport == "pgm" ? "pgm" : "epgm";
        if (!zlink_has (cap)) {
            print_result (
              lib_name, "MULTI_PUBSUB", transport, msg_size, 0.0, 0.0);
            cleanup ();
            return;
        }

        poll_timeout_ms = resolve_bench_count ("BENCH_PGM_POLL_TIMEOUT_MS", 50);
        set_sockopt_int (pub, ZLINK_OPT_SNDTIMEO, poll_timeout_ms,
                         "ZLINK_OPT_SNDTIMEO");
        for (void *sub : subs) {
            set_sockopt_int (
              sub, ZLINK_OPT_RCVTIMEO, poll_timeout_ms, "ZLINK_OPT_RCVTIMEO");
        }
    } else {
        poll_timeout_ms = resolve_bench_count ("BENCH_PUBSUB_POLL_TIMEOUT_MS", 50);
    }
    const int measure_poll_timeout_ms =
      resolve_bench_count ("BENCH_MULTI_PUBSUB_MEASURE_POLL_TIMEOUT_MS", 1);

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

        if (!run_pubsub_warmup (
              pub, subs, settings, buffer, recv_buf, poll_timeout_ms)) {
            print_result (
              lib_name, "MULTI_PUBSUB", transport, current_size, 0.0, 0.0);
            cleanup ();
            return;
        }

        const pubsub_measure_result_t bench = run_pubsub_measure (
          pub, subs, settings, buffer, recv_buf, measure_poll_timeout_ms);

        double latency = 0.0;
        ctx_guard_t lat_ctx;
        if (lat_ctx.valid ()) {
            latency = measure_pubsub_latency_us (
              lat_ctx.get (),
              transport,
              lib_name,
              current_size,
              pubsub_hwm,
              settings.connect_ready_timeout_ms,
              poll_timeout_ms);
        }

        const double throughput =
          !bench.failed && bench.measure_recv > 0
            ? static_cast<double> (bench.measure_recv)
                / static_cast<double> (std::max (1, settings.duration_seconds))
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
