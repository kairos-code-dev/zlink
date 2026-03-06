// GATEWAY multi client benchmark: gateway service echo workload.
// Topology: client gateway_t(connect to discovery, N) <-> server receiver_t(bind, 1)
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

namespace {

bool configure_gateway_tls (zlink::service::gateway_t &gateway,
                            const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    std::string cert;
    std::string key;
    std::string ca;
    if (!perf::multi::try_resolve_perf_tls_paths (cert, key, ca))
        return false;

    return gateway.set_tls_client (ca, "localhost", 0) == 0;
}

bool wait_discovery_ready (zlink::service::discovery_t &discovery,
                           const std::string &service,
                           int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (timeout_ms);
    while (std::chrono::steady_clock::now () < deadline) {
        if (discovery.service_available (service) > 0)
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
    return false;
}

bool wait_gateway_ready (zlink::service::gateway_t &gateway,
                         const std::string &service,
                         int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (std::max (timeout_ms, 1000));
    while (std::chrono::steady_clock::now () < deadline) {
        if (gateway.connection_count (service) > 0)
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }

    return gateway.connection_count (service) > 0;
}

} // namespace

void perf_gateway_client (const std::string &transport,
                          size_t msg_size,
                          const std::string &endpoint)
{
    perf::multi::set_perf_pattern_env ("GATEWAY");

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED,MULTI_GATEWAY," << transport << std::endl;
        return;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();

    perf::multi::ctx_guard_t ctx;
    zlink::service::discovery_t discovery (ctx.ctx (),
                                           zlink::service_type::gateway);
    if (!discovery.valid () || discovery.connect_registry (endpoint) != 0
        || discovery.subscribe ("svc") != 0) {
        return;
    }

    if (!wait_discovery_ready (discovery, "svc", 3000))
        return;

    std::vector<std::unique_ptr<zlink::service::gateway_t> > holders;
    std::vector<zlink::service::gateway_t *> gateways;
    std::vector<zlink::socket_t> gateway_router_wrappers;
    std::vector<zlink::socket_t *> gateway_routers;
    holders.reserve (settings.clients);
    gateways.reserve (settings.clients);
    gateway_router_wrappers.reserve (settings.clients);
    gateway_routers.reserve (settings.clients);

    for (size_t i = 0; i < settings.clients; ++i) {
        std::string routing_id = std::string ("gw_") + std::to_string (i);
        holders.emplace_back (
          new zlink::service::gateway_t (ctx.ctx (), discovery, routing_id));
        zlink::service::gateway_t &gateway = *holders.back ();

        if (!gateway.valid ())
            return;

        (void) gateway.set_sockopt (zlink::socket_options::sndhwm, settings.sndhwm);
        (void) gateway.set_sockopt (zlink::socket_options::rcvhwm, settings.rcvhwm);
        (void) gateway.set_sockopt (zlink::socket_options::sndtimeo,
                                    settings.sndtimeo_ms);
        (void) gateway.set_sockopt (zlink::socket_options::rcvtimeo,
                                    settings.rcvtimeo_ms);

        if (!configure_gateway_tls (gateway, transport))
            return;

        if (!wait_gateway_ready (
              gateway, "svc", settings.connect_ready_timeout_ms)) {
            return;
        }

        gateway_router_wrappers.emplace_back (
          zlink::socket_t::wrap (gateway.router_handle ()));
        gateway_routers.push_back (&gateway_router_wrappers.back ());
        gateways.push_back (&gateway);
    }

    perf::multi::settle ();

    std::vector<char> payload (
      std::max<size_t> (msg_size, perf_metric::header_size ()), 'g');
    const uint32_t run_id = static_cast<uint32_t> (perf_metric::now_us ());
    uint64_t seq = 1;

    auto recv_gateway_payload_header =
      [&] (zlink::socket_t &gateway_router,
           size_t payload_size,
           zlink::recv_flag flags,
           perf_metric::header_t *header_out,
           bool *header_ok_out) -> int {
        if (header_ok_out)
            *header_ok_out = false;

        zlink::message_t first;
        const int first_rc = gateway_router.recv (first, flags);
        if (first_rc < 0) {
            const int err = errno;
            if (err == EAGAIN || err == EINTR)
                return 0;
            return -1;
        }

        zlink::message_t payload_part;
        if (first.more ()) {
            if (gateway_router.recv (payload_part, zlink::recv_flag::none) < 0)
                return -1;
            if (payload_part.more () || payload_part.size () != payload_size)
                return -1;
        } else {
            if (first.size () != payload_size)
                return -1;
            payload_part = std::move (first);
        }

        bool header_ok = false;
        if (header_out) {
            header_ok = perf_metric::decode_payload_header (
              payload_part.data (), payload_part.size (), header_out);
        }

        if (header_ok_out)
            *header_ok_out = header_ok;
        return 1;
    };

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

        if (gateways.empty () || gateway_routers.size () != gateways.size ())
            return false;

        unsigned long long count = 0;
        size_t index = 0;
        const auto deadline = std::chrono::steady_clock::now ()
                              + std::chrono::seconds (seconds);

        while (std::chrono::steady_clock::now () < deadline) {
            const size_t slot = index % gateways.size ();
            zlink::service::gateway_t *gateway = gateways[slot];
            zlink::socket_t *gateway_router = gateway_routers[slot];
            ++index;
            if (!gateway || !gateway_router)
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

            if (gateway->send (
                  "svc", payload.data (), payload.size (), zlink::send_flag::none)
                != 0) {
                const int err = errno;
                if (err == EAGAIN || err == EINTR)
                    continue;
                return false;
            }

            perf_metric::header_t header;
            bool header_ok = false;
            // gateway router는 multipart(single/multi) 모두 가능하므로 통합 파싱.
            const int recv_rc = recv_gateway_payload_header (
              *gateway_router,
              payload.size (),
              zlink::recv_flag::none,
              &header,
              &header_ok);
            if (recv_rc == 0)
                continue;
            if (recv_rc < 0)
                return false;
            if (!header_ok)
                continue;
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
        if (settle_ms <= 0 || gateway_routers.empty ())
            return true;

        size_t index = 0;
        const auto deadline = std::chrono::steady_clock::now ()
                              + std::chrono::milliseconds (settle_ms);
        while (std::chrono::steady_clock::now () < deadline) {
            zlink::socket_t *gateway_router =
              gateway_routers[index % gateway_routers.size ()];
            ++index;
            if (!gateway_router)
                continue;

            perf_metric::header_t header;
            bool header_ok = false;
            const int recv_rc = recv_gateway_payload_header (
              *gateway_router,
              std::max<size_t> (msg_size, perf_metric::header_size ()),
              zlink::recv_flag::dontwait,
              &header,
              &header_ok);
            if (recv_rc == 0) {
                // Settle drain은 busy-spin 방지를 위해 짧은 sleep을 둔다.
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
                continue;
            }
            if (recv_rc < 0)
                return false;
            if (!header_ok)
                continue;
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

    if (!gateways.empty () && gateways[0]) {
        const char *stop = perf::multi::k_stop_token;
        const size_t stop_len = std::strlen (stop);
        for (int attempt = 0; attempt < 3; ++attempt) {
            const int rc =
              gateways[0]->send ("svc", stop, stop_len, zlink::send_flag::none);
            if (rc == 0)
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
                               "MULTI_GATEWAY",
                               transport,
                               msg_size,
                               throughput,
                               bandwidth,
                               lat.mean_us,
                               lat.p95_us,
                               lat.p99_us);
}
