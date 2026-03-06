// SPOT multi client benchmark: one-way spot subscriber receive workload.
// Topology: server spot_node(pub bind, 1) -> client spot_node(sub connect, N)
// Measurement: active-phase receive throughput + header-based latency sample.

#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_entry.hpp"
#include "../common/perf_metric_header.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

bool configure_spot_client_tls (zlink::service::spot_node_t &node,
                                const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    std::string cert;
    std::string key;
    std::string ca;
    if (!perf::multi::try_resolve_perf_tls_paths (cert, key, ca))
        return false;

    return node.set_tls_client (ca, "localhost", 0) == 0;
}

bool wait_sub_peer_ready (zlink::service::spot_node_t &node, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (std::max (timeout_ms, 1000));
    while (std::chrono::steady_clock::now () < deadline) {
        size_t peer_count = 0;
        if (node.sub_peers (NULL, &peer_count) == 0 && peer_count > 0)
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }

    size_t peer_count = 0;
    return node.sub_peers (NULL, &peer_count) == 0 && peer_count > 0;
}

} // namespace

void perf_spot_client (const std::string &transport,
                       size_t msg_size,
                       const std::string &endpoint)
{
    perf::multi::set_perf_pattern_env ("SPOT");

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED,MULTI_SPOT," << transport << std::endl;
        return;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();
    const size_t spot_client_count = settings.clients;

    perf::multi::ctx_guard_t ctx;
    std::vector<std::unique_ptr<zlink::service::spot_node_t> > nodes;
    std::vector<std::unique_ptr<zlink::service::spot_t> > spots;
    std::vector<zlink::service::spot_t *> spot_views;
    zlink::service::spot_node_t *ready_node = NULL;
    nodes.reserve (spot_client_count);
    spots.reserve (spot_client_count);
    spot_views.reserve (spot_client_count);

    for (size_t i = 0; i < spot_client_count; ++i) {
        nodes.emplace_back (new zlink::service::spot_node_t (ctx.ctx ()));
        zlink::service::spot_node_t &node = *nodes.back ();

        (void) node.set_sockopt (
          zlink::spot_node_socket_role::sub,
          zlink::socket_options::sndhwm,
          settings.sndhwm);
        (void) node.set_sockopt (
          zlink::spot_node_socket_role::sub,
          zlink::socket_options::rcvhwm,
          settings.rcvhwm);
        (void) node.set_sockopt (
          zlink::spot_node_socket_role::sub,
          zlink::socket_options::sndtimeo,
          settings.sndtimeo_ms);
        (void) node.set_sockopt (
          zlink::spot_node_socket_role::sub,
          zlink::socket_options::rcvtimeo,
          settings.rcvtimeo_ms);

        if (!configure_spot_client_tls (node, transport)
            || node.connect_peer_pub (endpoint) != 0) {
            return;
        }

        spots.emplace_back (new zlink::service::spot_t (node));
        if (!spots.back () || !spots.back ()->valid ()
            || spots.back ()->subscribe ("bench") != 0) {
            return;
        }

        if (!ready_node)
            ready_node = &node;
        spot_views.push_back (spots.back ().get ());
    }

    if (spot_views.empty ())
        return;

    if (ready_node)
        (void) wait_sub_peer_ready (*ready_node, settings.connect_ready_timeout_ms);

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

        if (spot_views.empty ())
            return false;

        unsigned long long count = 0;
        size_t index = 0;
        std::vector<zlink::message_t> parts;
        std::string topic;

        const bool active_phase = phase == perf_metric::phase_active;
        auto deadline = std::chrono::steady_clock::now () + duration;
        const auto active_search_deadline = deadline + std::chrono::seconds (2);
        // Active frame를 확인할 때까지는 탐색 모드로 수신만 수행한다.
        bool active_started = !active_phase;

        while (std::chrono::steady_clock::now ()
               < (active_started ? deadline : active_search_deadline)) {
            zlink::service::spot_t *spot = spot_views[index % spot_views.size ()];
            ++index;
            if (!spot)
                continue;

            parts.clear ();
            topic.clear ();
            if (spot->recv (parts, topic, zlink::recv_flag::none) != 0) {
                const int err = errno;
                if (err == EAGAIN || err == EINTR)
                    continue;
                return false;
            }
            if (topic != "bench" || parts.size () != 1)
                continue;

            perf_metric::header_t header;
            if (!perf_metric::decode_payload_header (
                  parts[0].data (), parts[0].size (), &header)) {
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
                // Active 측정 구간의 유효 프레임 sent_ts_us를 latency로 샘플링.
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
                               "MULTI_SPOT",
                               transport,
                               msg_size,
                               throughput,
                               bandwidth,
                               lat.mean_us,
                               lat.p95_us,
                               lat.p99_us);
}
