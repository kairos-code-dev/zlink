// DEALER-DEALER multi server benchmark: one-way receive sink.
// Topology: client DEALER(connect, N) -> server DEALER(bind, 1)
// Measurement role: drain incoming payloads and emit server queue metrics.

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"
#include "../common/perf_metric_header.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <vector>

bool perf_dealer_dealer_server (const std::string &lib_name,
                                const std::string &transport,
                                size_t msg_size)
{
    perf::multi::set_perf_pattern_env ("DEALER_DEALER");

    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << ",MULTI_DEALER_DEALER,"
                  << transport
                  << std::endl;
        return true;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();

    perf::multi::ctx_guard_t ctx;
    perf::multi::socket_guard_t server (ctx, zlink::socket_type::dealer);
    if (!server.valid ())
        return false;

    perf::multi::apply_benchmark_socket_options (
      server.sock (), settings, transport);
    if (!perf::multi::setup_tls_server (server.sock (), transport))
        return false;

    const std::string endpoint = perf::multi::bind_and_resolve_endpoint (
      server.sock (), transport, "cpp_multi_dealer_dealer", settings.server_bind_port);
    if (endpoint.empty ())
        return false;

    const bench_multi_cpu_sample_t resource_probe_start =
      perf::multi::start_resource_probe ();
    perf::multi::print_ready (endpoint);

    if (!perf::multi::wait_for_start_from_stdin (msg_size))
        return false;

    const int active_seconds =
      settings.duration_seconds > 0 ? settings.duration_seconds : 1;
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::seconds (active_seconds);

    zlink::poller_t poller;
    (void) poller.add (server.sock (), zlink::poll_event::pollin, &server.sock ());
    std::vector<zlink::poll_event_t> events;
    events.reserve (1);
    bool stop_requested = false;
    bool failed = false;
    unsigned long long active_count = 0;
    perf::multi::bench_latency_sampler_t latency;
    while (!stop_requested && std::chrono::steady_clock::now () < deadline) {
        const auto now = std::chrono::steady_clock::now ();
        long wait_ms = 100;
        const long remain_ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                                 deadline - now)
                                 .count ();
        if (remain_ms < wait_ms)
            wait_ms = remain_ms;
        if (wait_ms < 1)
            wait_ms = 1;

        const int poll_rc = poller.wait_all (events, wait_ms);
        if (poll_rc < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            failed = true;
            break;
        }
        if (poll_rc == 0)
            continue;

        for (size_t i = 0; i < events.size () && !stop_requested; ++i) {
            ::perf::socket_t *sock =
              static_cast<::perf::socket_t *> (events[i].user);
            if (!sock)
                continue;

            for (;;) {
                zlink::message_t inbound;
                const int rc =
                  sock->recv (inbound, zlink::recv_flags_t::dontwait);
                if (rc < 0) {
                    const int err = errno;
                    if (err == EAGAIN)
                        break;
                    if (err == EINTR)
                        continue;
                    stop_requested = true;
                    failed = true;
                    break;
                }

                if (perf::multi::is_stop_token (inbound.data (), inbound.size ())) {
                    stop_requested = true;
                    break;
                }

                perf_metric::header_t header;
                if (!perf_metric::decode_payload_header (
                      inbound.data (), inbound.size (), &header)) {
                    continue;
                }
                if (!perf_metric::is_expected (
                      header, 1U, perf_metric::phase_active, msg_size)) {
                    continue;
                }

                ++active_count;
                const uint64_t now_ns = perf_metric::now_ns ();
                const double sample_ns =
                  now_ns >= header.sent_ts_ns
                    ? static_cast<double> (now_ns - header.sent_ts_ns)
                    : 0.0;
                latency.add (sample_ns);
            }
        }
    }

    const bench_multi_resource_metrics_t resource_metrics =
      perf::multi::finish_resource_probe (resource_probe_start);
    if (failed || active_count == 0 || latency.count () == 0)
        return false;

    const perf::multi::bench_latency_stats_t latency_stats = latency.snapshot ();
    const double throughput =
      static_cast<double> (active_count)
      / static_cast<double> (std::max (1, active_seconds));
    const double bandwidth =
      throughput * static_cast<double> (msg_size) / 1000000.0;
    perf::multi::print_result (lib_name,
                               "MULTI_DEALER_DEALER",
                               transport,
                               msg_size,
                               throughput,
                               bandwidth,
                               latency_stats.mean_ns,
                               latency_stats.p95_ns,
                               latency_stats.p99_ns);
    perf::multi::print_server_resource_metrics (
      "current",
      "MULTI_DEALER_DEALER",
      transport,
      msg_size,
      resource_metrics);
    perf::multi::print_server_queue_metrics (
      "current",
      "MULTI_DEALER_DEALER",
      transport,
      msg_size,
      perf::multi::server_queue_stats_t ());
    return true;
}

int main (int argc, char **argv)
{
    if (argc < 4) {
        std::cerr << "usage: <lib_name> <transport> <size>" << std::endl;
        return 1;
    }

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t size = static_cast<size_t> (std::strtoull (argv[3], NULL, 10));
    if (size == 0)
        return 1;

    return perf_dealer_dealer_server (lib_name, transport, size) ? 0 : 1;
}
