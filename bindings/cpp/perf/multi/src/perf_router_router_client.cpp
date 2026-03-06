// ROUTER-ROUTER multi client benchmark: routed echo request/reply workload.
// Topology: client ROUTER(connect, N) <-> server ROUTER(bind, routing_id=SERVER)
// Measurement: active-phase echo throughput + RTT latency from payload header.

#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_entry.hpp"
#include "../common/perf_metric_header.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

void perf_router_router_client (const std::string &transport,
                                size_t msg_size,
                                const std::string &endpoint)
{
    perf::multi::set_perf_pattern_env ("ROUTER_ROUTER");

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED,MULTI_ROUTER_ROUTER," << transport << std::endl;
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
          new perf::multi::socket_guard_t (ctx, zlink::socket_type::router));
        zlink::socket_t &sock = holders.back ()->sock ();

        (void) sock.set (
          zlink::socket_options::routing_id, std::string ("rr_") + std::to_string (i));
        perf::multi::apply_benchmark_socket_options (sock, settings, transport);
        if (!perf::multi::setup_tls_client (sock, transport))
            return;
        if (sock.connect (endpoint) != 0)
            return;

        sockets.push_back (&sock);
    }

    perf::multi::settle ();

    const std::string server_id = "SERVER";
    std::vector<char> payload (
      std::max<size_t> (msg_size, perf_metric::header_size ()), 'r');
    const uint32_t run_id = static_cast<uint32_t> (perf_metric::now_us ());
    uint64_t seq = 1;

    auto run_phase = [&] (perf_metric::phase_t phase,
                          int seconds,
                          unsigned long long *count_out,
                          perf::multi::bench_latency_sampler_t *lat_out) -> bool {
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

            const int id_sent = sock->send (
              server_id.data (), server_id.size (), zlink::send_flag::sndmore);
            if (id_sent != static_cast<int> (server_id.size ())) {
                const int err = errno;
                if (err == EAGAIN || err == EINTR)
                    continue;
                return false;
            }

            const int data_sent =
              sock->send (payload.data (), payload.size (), zlink::send_flag::none);
            if (data_sent != static_cast<int> (payload.size ())) {
                const int err = errno;
                if (err == EAGAIN || err == EINTR)
                    continue;
                return false;
            }

            zlink::message_t reply_routing_id;
            if (sock->recv (reply_routing_id, zlink::recv_flag::none) < 0) {
                const int err = errno;
                if (err == EAGAIN || err == EINTR)
                    continue;
                return false;
            }
            if (!reply_routing_id.more ())
                continue;

            zlink::message_t reply_payload;
            if (sock->recv (reply_payload, zlink::recv_flag::none) < 0) {
                const int err = errno;
                if (err == EAGAIN || err == EINTR)
                    continue;
                return false;
            }
            if (reply_payload.more ())
                continue;

            perf_metric::header_t header;
            if (!perf_metric::decode_payload_header (
                  reply_payload.data (), reply_payload.size (), &header)) {
                continue;
            }
            if (!perf_metric::is_expected (header, run_id, phase, msg_size))
                continue;

            ++count;
            if (lat_out && phase == perf_metric::phase_active) {
                const uint64_t now = perf_metric::now_us ();
                const double latency_us = now >= header.sent_ts_us
                                            ? static_cast<double> (
                                                now - header.sent_ts_us)
                                            : 0.0;
                lat_out->add (latency_us);
            }
        }

        *count_out = count;
        return count > 0;
    };

    auto run_settle_drain_phase = [&] (int settle_ms) -> bool {
        if (settle_ms <= 0 || sockets.empty ())
            return true;

        size_t index = 0;
        const auto deadline = std::chrono::steady_clock::now ()
                              + std::chrono::milliseconds (settle_ms);
        while (std::chrono::steady_clock::now () < deadline) {
            zlink::socket_t *sock = sockets[index % sockets.size ()];
            ++index;
            if (!sock)
                continue;

            zlink::message_t reply_routing_id;
            if (sock->recv (reply_routing_id, zlink::recv_flag::dontwait) < 0) {
                const int err = errno;
                if (err == EAGAIN || err == EINTR) {
                    // Settle drain은 큐 비우기 목적이며 1ms sleep으로 busy-spin 억제.
                    std::this_thread::sleep_for (std::chrono::milliseconds (1));
                    continue;
                }
                return false;
            }
            if (!reply_routing_id.more ())
                continue;

            zlink::message_t reply_payload;
            if (sock->recv (reply_payload, zlink::recv_flag::none) < 0) {
                const int err = errno;
                if (err == EAGAIN || err == EINTR)
                    continue;
                return false;
            }
            if (reply_payload.more ())
                continue;

            perf_metric::header_t header;
            if (!perf_metric::decode_payload_header (
                  reply_payload.data (), reply_payload.size (), &header)) {
                continue;
            }
            if (header.magic != perf_metric::k_magic
                || header.phase != static_cast<uint32_t> (perf_metric::phase_warmup)
                || header.msg_size != static_cast<uint32_t> (msg_size)
                || header.run_id != run_id) {
                continue;
            }
        }

        return true;
    };

    unsigned long long warmup_count = 0;
    if (!run_phase (perf_metric::phase_warmup,
                    std::max (0, settings.warmup_seconds),
                    &warmup_count,
                    NULL)) {
        return;
    }

    if (!run_settle_drain_phase (std::max (0, settings.settle_ms))) {
        return;
    }

    perf::multi::bench_latency_sampler_t latency;
    unsigned long long active_count = 0;
    const int active_seconds = std::max (1, settings.duration_seconds);
    if (!run_phase (perf_metric::phase_active,
                    active_seconds,
                    &active_count,
                    &latency)) {
        return;
    }

    if (!sockets.empty () && sockets[0]) {
        const char *stop = perf::multi::k_stop_token;
        const size_t stop_len = std::strlen (stop);
        for (int attempt = 0; attempt < 3; ++attempt) {
            const int stop_id_sent = sockets[0]->send (
              server_id.data (), server_id.size (), zlink::send_flag::sndmore);
            if (stop_id_sent != static_cast<int> (server_id.size ())) {
                const int err = errno;
                if (err != EAGAIN && err != EINTR)
                    break;
                std::this_thread::sleep_for (std::chrono::milliseconds (2));
                continue;
            }

            const int stop_sent =
              sockets[0]->send (stop, stop_len, zlink::send_flag::none);
            if (stop_sent == static_cast<int> (stop_len))
                break;
            const int err = errno;
            if (err != EAGAIN && err != EINTR)
                break;
            std::this_thread::sleep_for (std::chrono::milliseconds (2));
        }
    }

    const perf::multi::bench_latency_stats_t lat = latency.snapshot ();
    const double throughput =
      static_cast<double> (active_count) / static_cast<double> (active_seconds);
    const double bandwidth =
      throughput * static_cast<double> (msg_size) * 2.0 / 1000000.0;

    perf::multi::print_result ("current",
                               "MULTI_ROUTER_ROUTER",
                               transport,
                               msg_size,
                               throughput,
                               bandwidth,
                               lat.mean_us,
                               lat.p95_us,
                               lat.p99_us);
}
