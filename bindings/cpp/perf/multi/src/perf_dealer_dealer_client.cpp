// DEALER-DEALER multi client benchmark: one-way DEALER send workload.
// Topology: client DEALER(connect, N) -> server DEALER(bind, 1)
// Measurement: active-phase send throughput + sender-side send latency sample.

#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_entry.hpp"
#include "../common/perf_metric_header.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

void perf_dealer_dealer_client (const std::string &transport,
                                size_t msg_size,
                                const std::string &endpoint)
{
    perf::multi::set_perf_pattern_env ("DEALER_DEALER");

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED,MULTI_DEALER_DEALER," << transport << std::endl;
        return;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();

    perf::multi::ctx_guard_t ctx;
    std::vector<std::unique_ptr<perf::multi::socket_guard_t> > holders;
    std::vector<zlink::socket_t *> sockets;
    holders.reserve (settings.clients);
    sockets.reserve (settings.clients);

    for (size_t i = 0; i < settings.clients; ++i) {
        holders.emplace_back (
          new perf::multi::socket_guard_t (ctx, zlink::socket_type::dealer));
        zlink::socket_t &sock = holders.back ()->sock ();

        perf::multi::apply_benchmark_socket_options (sock, settings, transport);
        if (!perf::multi::setup_tls_client (sock, transport))
            return;
        if (sock.connect (endpoint) != 0)
            return;

        sockets.push_back (&sock);
    }

    perf::multi::settle ();

    std::vector<char> payload (
      std::max<size_t> (msg_size, perf_metric::header_size ()), 'd');
    const uint32_t run_id = static_cast<uint32_t> (perf_metric::now_us ());
    uint64_t seq = 1;

    auto run_send_phase = [&] (perf_metric::phase_t phase,
                               int seconds,
                               unsigned long long *count_out,
                               perf::multi::bench_latency_sampler_t *lat_out)
      -> bool {
        if (!count_out)
            return false;

        if (seconds <= 0) {
            *count_out = 0;
            return true;
        }

        if (sockets.empty ())
            return false;

        unsigned long long count = 0;
        size_t index = 0;

        const auto deadline = std::chrono::steady_clock::now ()
                              + std::chrono::seconds (seconds);
        while (std::chrono::steady_clock::now () < deadline) {
            zlink::socket_t *sock = sockets[index % sockets.size ()];
            ++index;
            if (!sock)
                continue;

            const uint64_t sent_ts = perf_metric::now_us ();
            if (!perf_metric::stamp_payload (payload.data (),
                                             payload.size (),
                                             run_id,
                                             phase,
                                             msg_size,
                                             seq++,
                                             sent_ts)) {
                continue;
            }

            const auto t0 = std::chrono::steady_clock::now ();
            const int sent =
              sock->send (payload.data (), payload.size (), zlink::send_flag::none);
            if (sent != static_cast<int> (payload.size ())) {
                const int err = errno;
                if (err == EAGAIN || err == EINTR)
                    continue;
                return false;
            }

            ++count;
            if (lat_out && phase == perf_metric::phase_active) {
                const auto t1 = std::chrono::steady_clock::now ();
                const double us = static_cast<double> (
                  std::chrono::duration_cast<std::chrono::microseconds> (t1 - t0)
                    .count ());
                lat_out->add (us);
            }
        }

        *count_out = count;
        return true;
    };

    auto run_settle_phase = [&] (int settle_ms) {
        if (settle_ms <= 0)
            return;
        const auto deadline = std::chrono::steady_clock::now ()
                              + std::chrono::milliseconds (settle_ms);
        // One-way sender는 settle에서 수신 드레인이 불가하므로 시간 기반 대기만 수행.
        while (std::chrono::steady_clock::now () < deadline)
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
    };

    unsigned long long warmup_count = 0;
    if (!run_send_phase (perf_metric::phase_warmup,
                         std::max (0, settings.warmup_seconds),
                         &warmup_count,
                         NULL)) {
        return;
    }

    run_settle_phase (std::max (0, settings.settle_ms));

    perf::multi::bench_latency_sampler_t latency;
    unsigned long long active_count = 0;
    const int active_seconds = std::max (1, settings.duration_seconds);
    if (!run_send_phase (perf_metric::phase_active,
                         active_seconds,
                         &active_count,
                         &latency)) {
        return;
    }

    if (!sockets.empty () && sockets[0]) {
        const char *stop = perf::multi::k_stop_token;
        const size_t stop_len = std::strlen (stop);
        for (int attempt = 0; attempt < 3; ++attempt) {
            const int sent =
              sockets[0]->send (stop, stop_len, zlink::send_flag::none);
            if (sent == static_cast<int> (stop_len))
                break;
            const int err = errno;
            if (err != EAGAIN && err != EINTR)
                break;
            std::this_thread::sleep_for (std::chrono::milliseconds (2));
        }
    }

    if (active_count == 0)
        return;

    const perf::multi::bench_latency_stats_t lat = latency.snapshot ();
    const double throughput =
      static_cast<double> (active_count) / static_cast<double> (active_seconds);
    const double bandwidth = throughput * static_cast<double> (msg_size) / 1000000.0;

    perf::multi::print_result ("current",
                               "MULTI_DEALER_DEALER",
                               transport,
                               msg_size,
                               throughput,
                               bandwidth,
                               lat.mean_us,
                               lat.p95_us,
                               lat.p99_us);
}
