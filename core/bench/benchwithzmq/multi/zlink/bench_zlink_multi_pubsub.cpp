#include "../common/bench_common_zlink.hpp"
#include "../common/bench_common_multi.hpp"
#include <zlink.h>
#include <vector>
#include <cerrno>
#include <algorithm>

namespace {

multi_send_result_t send_pub_nonblocking (void *pub,
                                          const std::vector<char> &buffer)
{
    if (zlink_send (pub, buffer.data (), buffer.size (), ZLINK_DONTWAIT) >= 0)
        return multi_send_ok;
    const int err = zlink_errno ();
    if (err == ETERM || err == ENOTSOCK)
        return multi_send_error;
    return multi_send_would_block;
}

int recv_batch_subscribers (const std::vector<void *> &subs,
                            std::vector<zlink_pollitem_t> &poll_items,
                            std::vector<char> &recv_buf,
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
    for (size_t i = 0; i < subs.size () && received < recv_batch; ++i) {
        if ((poll_items[i].revents & ZLINK_POLLIN) == 0)
            continue;
        if (zlink_recv (subs[i], recv_buf.data (), recv_buf.size (), 0) < 0)
            return -1;
        ++received;
    }

    while (received < recv_batch) {
        bool got_any = false;
        for (size_t i = 0; i < subs.size () && received < recv_batch; ++i) {
            const int rc = zlink_recv (
              subs[i], recv_buf.data (), recv_buf.size (), ZLINK_DONTWAIT);
            if (rc >= 0) {
                ++received;
                got_any = true;
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

double measure_pubsub_latency_us (void *ctx,
                                  const std::string &transport,
                                  const std::string &lib_name,
                                  size_t msg_size,
                                  int hwm,
                                  int ready_timeout_ms,
                                  int poll_timeout_ms)
{
    void *pub = zlink_socket (ctx, ZLINK_PUB);
    void *sub = zlink_socket (ctx, ZLINK_SUB);
    if (!pub || !sub) {
        if (sub)
            zlink_close (sub);
        if (pub)
            zlink_close (pub);
        return 0.0;
    }

    const int linger_ms = 0;
    set_sockopt_int (pub, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
    set_sockopt_int (sub, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
    apply_benchmark_hwm (pub, hwm);
    apply_benchmark_hwm (sub, hwm);
    zlink_setsockopt (sub, ZLINK_SUBSCRIBE, "", 0);

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
    auto start = std::chrono::steady_clock::now ();
    for (int i = 0; i < lat_count; ++i) {
        if (zlink_send (pub, buffer.data (), buffer.size (), 0) < 0)
            continue;

        zlink_pollitem_t item[] = {{sub, 0, ZLINK_POLLIN, 0}};
        if (zlink_poll (item, 1, poll_timeout_ms) <= 0
            || (item[0].revents & ZLINK_POLLIN) == 0)
            continue;

        if (zlink_recv (sub, recv_buf.data (), recv_buf.size (), 0) >= 0)
            ++received;
    }

    double latency = 0.0;
    if (received > 0) {
        const auto elapsed = std::chrono::steady_clock::now () - start;
        latency =
          (std::chrono::duration<double, std::milli> (elapsed).count () * 1000.0)
          / static_cast<double> (received);
    }

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

    void *pub = zlink_socket (ctx.get (), ZLINK_PUB);
    if (!pub)
        return;

    const int linger_ms = 0;
    set_sockopt_int (pub, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
    apply_benchmark_hwm (pub, settings.hwm);

    std::vector<void *> subs (settings.clients, NULL);
    for (size_t i = 0; i < subs.size (); ++i) {
        subs[i] = zlink_socket (ctx.get (), ZLINK_SUB);
        if (!subs[i]) {
            for (size_t j = 0; j < i; ++j)
                zlink_close (subs[j]);
            zlink_close (pub);
            return;
        }
        zlink_setsockopt (subs[i], ZLINK_SUBSCRIBE, "", 0);
        set_sockopt_int (subs[i], ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
        apply_benchmark_hwm (subs[i], settings.hwm);
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
        set_sockopt_int (pub, ZLINK_SNDTIMEO, poll_timeout_ms, "ZLINK_SNDTIMEO");
        for (void *sub : subs) {
            set_sockopt_int (
              sub, ZLINK_RCVTIMEO, poll_timeout_ms, "ZLINK_RCVTIMEO");
        }
    } else {
        poll_timeout_ms = resolve_bench_count ("BENCH_PUBSUB_POLL_TIMEOUT_MS", 50);
    }

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

    std::vector<zlink_pollitem_t> poll_items;
    poll_items.reserve (subs.size ());
    for (size_t i = 0; i < subs.size (); ++i) {
        zlink_pollitem_t item = {subs[i], 0, ZLINK_POLLIN, 0};
        poll_items.push_back (item);
    }

    const std::vector<size_t> msg_sizes = resolve_bench_msg_sizes (msg_size);
    for (size_t s = 0; s < msg_sizes.size (); ++s) {
        const size_t current_size = msg_sizes[s];
        std::vector<char> buffer (std::max<size_t> (1, current_size), 'a');
        std::vector<char> recv_buf (std::max<size_t> (1, current_size));

        std::vector<void *> senders (1, pub);
        const multi_bench_result_t bench = run_multi_phase_benchmark (
          senders, settings,
          [&] (size_t) { return send_pub_nonblocking (pub, buffer); },
          [&] (multi_bench_phase_t) {
              return recv_batch_subscribers (
                subs, poll_items, recv_buf, settings.recv_batch, poll_timeout_ms);
          });

        const double latency = measure_pubsub_latency_us (
          ctx.get (),
          transport,
          lib_name,
          current_size,
          settings.hwm,
          settings.connect_ready_timeout_ms,
          poll_timeout_ms);

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
