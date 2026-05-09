// DEALER-DEALER multi server benchmark: one-way receive sink.
// Topology: client DEALER(connect, N) -> server DEALER(bind, 1)
// Measurement role: drain incoming payloads and emit server queue metrics.

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"
#include "../common/perf_metric_header.hpp"

#include <algorithm>
#include <any>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <optional>
#include <vector>

namespace {

void apply_dealer_socket_options (
  zlink::dealer_socket_t &socket,
  const perf::multi::multi_bench_settings_t &settings)
{
    zlink::dealer_socket_options_t options = socket.options ();
    if (perf::multi::manual_socket_overrides_enabled ()) {
        options.send_hwm (
          zlink::message_count_t::value (settings.sndhwm > 0 ? settings.sndhwm : 1));
        options.recv_hwm (
          zlink::message_count_t::value (settings.rcvhwm > 0 ? settings.rcvhwm : 1));
    }
    options.send_timeout (std::chrono::milliseconds (settings.sndtimeo_ms));
    options.recv_timeout (std::chrono::milliseconds (settings.rcvtimeo_ms));
    options.linger (std::chrono::milliseconds (0));
}

std::string bind_dealer_endpoint (zlink::dealer_socket_t &socket,
                                  const std::string &transport,
                                  int fixed_port)
{
    const std::string bind_endpoint =
      perf::multi::make_endpoint (
        transport, "cpp_multi_dealer_dealer", fixed_port);
    try {
        socket.bind (bind_endpoint);
    }
    catch (const zlink::zlink_error_t &) {
        return std::string ();
    }
    return transport == "inproc"
             ? bind_endpoint
             : perf::multi::normalize_endpoint_host (
                 socket.options ().last_endpoint ());
}

} // namespace

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

    try {
    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();

    perf::multi::ctx_guard_t ctx;
    zlink::dealer_socket_t server (ctx.ctx ());
    if (!server.valid ())
        return false;

    apply_dealer_socket_options (server, settings);
    if (!perf::multi::setup_tls_server (server, transport))
        return false;

    zlink::poller_t poller;
    poller.add (server, zlink::poll_event_flag_t::pollin);

    const std::string endpoint =
      bind_dealer_endpoint (server, transport, settings.server_bind_port);
    if (endpoint.empty ())
        return false;

    const bench_multi_cpu_sample_t resource_probe_start =
      perf::multi::start_resource_probe ();
    perf::multi::print_ready (endpoint);

    if (!perf::multi::wait_for_start_from_stdin (msg_size))
        return false;
    server.options ().auto_hwm_msg_unit_bytes (
      zlink::byte_size_t::bytes (static_cast<int64_t> (msg_size)));
    ctx.ctx ().recalculate_auto_hwm ();

    const int active_seconds =
      settings.duration_seconds > 0 ? settings.duration_seconds : 1;
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::seconds (active_seconds);

    bool stop_requested = false;
    bool failed = false;
    unsigned long long active_count = 0;
    perf::multi::bench_latency_sampler_t latency (
      static_cast<size_t> (active_seconds) * 5000000U);
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

        const std::optional<zlink::poll_event_t> event =
          poller.wait (std::chrono::milliseconds (wait_ms));
        if (!event)
            continue;
        if (!(static_cast<short> (event->revents)
              & static_cast<short> (zlink::poll_event_flag_t::pollin))) {
            continue;
        }

        for (;;) {
            const std::optional<zlink::received_t> received =
              server.recv (zlink::recv_flags_t::dontwait);
            if (!received)
                break;
            if (received->routing_id () || !received->is_single_part ()) {
                failed = true;
                break;
            }
            const zlink::message_t &inbound = received->parts ()[0];

            if (perf::multi::is_stop_token (
                  inbound.data (), inbound.size ())) {
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
        if (failed)
            break;
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
    catch (const zlink::zlink_error_t &) {
        return false;
    }
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
