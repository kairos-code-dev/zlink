// PUBSUB multi client benchmark: one-way subscriber receive workload.
// Topology: server PUB(bind, 1) -> client SUB(connect, N)
// Measurement: active-phase receive throughput + header-based latency sample.

#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_entry.hpp"
#include "../common/perf_metric_header.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <memory>
#include <vector>

void perf_pubsub_client (const std::string &transport,
                         size_t msg_size,
                         const std::string &endpoint)
{
    perf::multi::set_perf_pattern_env ("PUBSUB");

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED,MULTI_PUBSUB," << transport << std::endl;
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
          new perf::multi::socket_guard_t (ctx, zlink::socket_type::sub));
        zlink::socket_t &sock = holders.back ()->sock ();

        (void) sock.set (zlink::socket_options::subscribe, std::string ());
        perf::multi::apply_benchmark_socket_options (sock, settings, transport);
        if (!perf::multi::setup_tls_client (sock, transport))
            return;
        if (sock.connect (endpoint) != 0)
            return;

        sockets.push_back (&sock);
    }

    perf::multi::settle ();

    const uint32_t run_id = 1;

    auto run_recv_phase = [&] (perf_metric::phase_t phase,
                               std::chrono::milliseconds duration,
                               unsigned long long *count_out,
                               perf::multi::bench_latency_sampler_t *lat_out) -> bool {
        if (!count_out)
            return false;

        if (duration.count () <= 0) {
            *count_out = 0;
            return true;
        }

        if (sockets.empty ())
            return false;

        unsigned long long count = 0;
        size_t index = 0;

        const bool active_phase = phase == perf_metric::phase_active;
        auto deadline = std::chrono::steady_clock::now () + duration;
        const auto active_search_deadline = deadline + std::chrono::seconds (2);
        // Active frame를 처음 확인한 시점부터 측정 window를 시작한다.
        bool active_started = !active_phase;

        while (std::chrono::steady_clock::now ()
               < (active_started ? deadline : active_search_deadline)) {
            zlink::socket_t *sock = sockets[index % sockets.size ()];
            ++index;
            if (!sock)
                continue;

            zlink::message_t msg;
            if (sock->recv (msg, zlink::recv_flag::none) < 0) {
                const int err = errno;
                if (err == EAGAIN || err == EINTR)
                    continue;
                return false;
            }
            if (msg.more ())
                continue;

            perf_metric::header_t header;
            if (!perf_metric::decode_payload_header (
                  msg.data (), msg.size (), &header)) {
                continue;
            }
            if (header.magic != perf_metric::k_magic
                || header.run_id != run_id
                || header.msg_size != static_cast<uint32_t> (msg_size)) {
                continue;
            }

            if (active_phase) {
                if (!active_started
                    && header.phase
                         == static_cast<uint32_t> (perf_metric::phase_active)) {
                    active_started = true;
                    deadline = std::chrono::steady_clock::now () + duration;
                }
            } else if (header.phase != static_cast<uint32_t> (phase))
                continue;

            ++count;
            if (lat_out && phase == perf_metric::phase_active) {
                // Active 측정 구간에서 수신된 유효 프레임의 sent_ts_us를 샘플링한다.
                const uint64_t now = perf_metric::now_us ();
                const double latency_us = now >= header.sent_ts_us
                                            ? static_cast<double> (
                                                now - header.sent_ts_us)
                                            : 0.0;
                lat_out->add (latency_us);
            }
        }

        *count_out = count;
        return true;
    };

    unsigned long long warmup_count = 0;
    if (!run_recv_phase (perf_metric::phase_warmup,
                         std::chrono::seconds (std::max (0, settings.warmup_seconds)),
                         &warmup_count,
                         NULL)) {
        return;
    }

    unsigned long long drain_count = 0;
    if (!run_recv_phase (
          perf_metric::phase_drain,
          std::chrono::milliseconds (std::max (0, settings.settle_ms)),
          &drain_count,
          NULL)) {
        return;
    }

    perf::multi::bench_latency_sampler_t latency;
    unsigned long long active_count = 0;
    const int active_seconds = std::max (1, settings.duration_seconds);
    if (!run_recv_phase (perf_metric::phase_active,
                         std::chrono::seconds (active_seconds),
                         &active_count,
                         &latency)) {
        return;
    }

    if (active_count == 0)
        return;

    const perf::multi::bench_latency_stats_t lat = latency.snapshot ();
    const double throughput =
      static_cast<double> (active_count) / static_cast<double> (active_seconds);
    const double bandwidth = throughput * static_cast<double> (msg_size) / 1000000.0;

    perf::multi::print_result ("current",
                               "MULTI_PUBSUB",
                               transport,
                               msg_size,
                               throughput,
                               bandwidth,
                               lat.mean_us,
                               lat.p95_us,
                               lat.p99_us);
}
