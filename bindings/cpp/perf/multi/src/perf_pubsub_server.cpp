// PUBSUB multi server benchmark: one-way publisher source.
// Topology: server PUB(bind, 1) -> client SUB(connect, N)
// Measurement role: stamp payload phases and publish continuously.

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <vector>

void perf_pubsub_server (const std::string &transport, size_t msg_size)
{
    perf::multi::set_perf_pattern_env ("PUBSUB");

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED,MULTI_PUBSUB," << transport << std::endl;
        return;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();

    perf::multi::ctx_guard_t ctx;
    perf::multi::socket_guard_t publisher (ctx, zlink::socket_type::pub);
    if (!publisher.valid ())
        return;

    perf::multi::apply_benchmark_socket_options (
      publisher.sock (), settings, transport);
    if (!perf::multi::setup_tls_server (publisher.sock (), transport))
        return;

    const std::string endpoint = perf::multi::bind_and_resolve_endpoint (
      publisher.sock (), transport, "cpp_multi_pubsub", settings.server_bind_port);
    if (endpoint.empty ())
        return;

    perf::multi::print_ready (endpoint);

    std::vector<char> payload (
      std::max<size_t> (msg_size, perf_metric::header_size ()), 'p');
    const uint32_t run_id = 1;
    uint64_t seq = 1;

    auto run_phase = [&] (perf_metric::phase_t phase,
                          std::chrono::milliseconds duration) -> bool {
        if (duration.count () <= 0)
            return true;

        const auto deadline = std::chrono::steady_clock::now () + duration;
        while (std::chrono::steady_clock::now () < deadline) {
            (void) perf_metric::stamp_payload (payload.data (),
                                               payload.size (),
                                               run_id,
                                               phase,
                                               msg_size,
                                               seq++,
                                               perf_metric::now_us ());

            const int sent = publisher.sock ().send (
              payload.data (), payload.size (), zlink::send_flag::none);
            if (sent != static_cast<int> (payload.size ())) {
                const int err = errno;
                if (err == EAGAIN || err == EINTR)
                    continue;
                return false;
            }
        }
        return true;
    };

    if (!run_phase (perf_metric::phase_warmup,
                    std::chrono::seconds (std::max (0, settings.warmup_seconds))))
        return;
    if (!run_phase (
          perf_metric::phase_drain,
          std::chrono::milliseconds (std::max (0, settings.settle_ms))))
        return;
    if (!run_phase (perf_metric::phase_active,
                    std::chrono::seconds (std::max (1, settings.duration_seconds))))
        return;

    perf::multi::print_server_queue_metrics (
      "current",
      "MULTI_PUBSUB",
      transport,
      msg_size,
      perf::multi::server_queue_stats_t ());
}
