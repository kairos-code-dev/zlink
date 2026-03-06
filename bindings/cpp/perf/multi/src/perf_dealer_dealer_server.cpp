// DEALER-DEALER multi server benchmark: one-way receive sink.
// Topology: client DEALER(connect, N) -> server DEALER(bind, 1)
// Measurement role: drain incoming payloads and emit server queue metrics.

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"

#include <cerrno>
#include <chrono>

void perf_dealer_dealer_server (const std::string &transport, size_t)
{
    perf::multi::set_perf_pattern_env ("DEALER_DEALER");

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED,MULTI_DEALER_DEALER," << transport << std::endl;
        return;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();

    perf::multi::ctx_guard_t ctx;
    perf::multi::socket_guard_t server (ctx, zlink::socket_type::dealer);
    if (!server.valid ())
        return;

    perf::multi::apply_benchmark_socket_options (
      server.sock (), settings, transport);
    if (!perf::multi::setup_tls_server (server.sock (), transport))
        return;

    const std::string endpoint = perf::multi::bind_and_resolve_endpoint (
      server.sock (), transport, "cpp_multi_dealer_dealer", settings.server_bind_port);
    if (endpoint.empty ())
        return;

    perf::multi::print_ready (endpoint);

    const int warmup_seconds = settings.warmup_seconds > 0 ? settings.warmup_seconds : 0;
    const int active_seconds = settings.duration_seconds > 0 ? settings.duration_seconds : 1;
    const int settle_seconds =
      settings.settle_ms > 0 ? (settings.settle_ms + 999) / 1000 : 0;
    const int deadline_seconds =
      warmup_seconds + settle_seconds + active_seconds + 2;

    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::seconds (deadline_seconds);

    while (std::chrono::steady_clock::now () < deadline) {
        zlink::message_t msg;
        const int rc = server.sock ().recv (msg, zlink::recv_flag::none);
        if (rc < 0) {
            const int err = errno;
            if (err == EAGAIN || err == EINTR)
                continue;
            break;
        }

        if (perf::multi::is_stop_token_message (msg))
            break;
    }

    perf::multi::print_server_queue_metrics (
      "current",
      "MULTI_DEALER_DEALER",
      transport,
      0,
      perf::multi::server_queue_stats_t ());
}
