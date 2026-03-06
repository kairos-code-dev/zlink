// PUBSUB multi server benchmark: one-way publisher source.
// Topology: server PUB(bind, 1) -> client SUB(connect, N)
// Measurement role: stamp payload phases and publish continuously.

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <vector>

namespace {

long compute_wait_ms (const perf::multi::multi_bench_settings_t &settings,
                      const std::chrono::steady_clock::time_point &deadline)
{
    long wait_ms = settings.client_poll_timeout_ms > 0 ? settings.client_poll_timeout_ms : 100;
    const long remain_ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                             deadline - std::chrono::steady_clock::now ())
                             .count ();
    if (remain_ms < wait_ms)
        wait_ms = remain_ms;
    if (wait_ms < 1)
        wait_ms = 1;
    return wait_ms;
}

bool run_phase (zlink::socket_t &publisher,
                zlink::poller_t &poller,
                std::vector<zlink::poll_event_t> &events,
                std::vector<char> &payload,
                size_t msg_size,
                uint32_t run_id,
                uint64_t &seq,
                perf_metric::phase_t phase,
                std::chrono::steady_clock::duration duration,
                const perf::multi::multi_bench_settings_t &settings)
{
    if (duration <= std::chrono::steady_clock::duration::zero ())
        return true;

    bool pending = false;
    const auto deadline = std::chrono::steady_clock::now () + duration;
    while (std::chrono::steady_clock::now () < deadline) {
        if (!pending) {
            if (!perf_metric::stamp_payload (payload.data (),
                                             payload.size (),
                                             run_id,
                                             phase,
                                             msg_size,
                                             seq++,
                                             perf_metric::now_us ())) {
                return false;
            }
        }

        const int sent = publisher.send (
          payload.data (), payload.size (), zlink::send_flag::dontwait);
        if (sent == static_cast<int> (payload.size ())) {
            pending = false;
            if (poller.modify (publisher, static_cast<zlink::poll_event> (0)) != 0)
                return false;
            continue;
        }

        if (sent < 0 && errno == EAGAIN) {
            pending = true;
            if (poller.modify (publisher, zlink::poll_event::pollout) != 0)
                return false;
        } else {
            return false;
        }

        const int poll_rc = poller.wait (events, compute_wait_ms (settings, deadline));
        if (poll_rc < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (poll_rc == 0)
            continue;
    }

    return true;
}

} // namespace

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
    zlink::poller_t poller;
    std::vector<zlink::poll_event_t> events;
    events.reserve (1);
    (void) poller.add (publisher.sock (), zlink::poll_event::pollout);
    (void) poller.modify (publisher.sock (), static_cast<zlink::poll_event> (0));

    if (!run_phase (publisher.sock (),
                    poller,
                    events,
                    payload,
                    msg_size,
                    run_id,
                    seq,
                    perf_metric::phase_warmup,
                    std::chrono::seconds (std::max (0, settings.warmup_seconds)),
                    settings))
        return;
    if (!run_phase (publisher.sock (),
                    poller,
                    events,
                    payload,
                    msg_size,
                    run_id,
                    seq,
                    perf_metric::phase_drain,
                    std::chrono::milliseconds (std::max (0, settings.settle_ms)),
                    settings))
        return;
    if (!run_phase (publisher.sock (),
                    poller,
                    events,
                    payload,
                    msg_size,
                    run_id,
                    seq,
                    perf_metric::phase_active,
                    std::chrono::seconds (std::max (1, settings.duration_seconds)),
                    settings))
        return;

    perf::multi::print_server_queue_metrics (
      "current",
      "MULTI_PUBSUB",
      transport,
      msg_size,
      perf::multi::server_queue_stats_t ());
}

int main (int argc, char **argv)
{
    if (argc < 3) {
        std::cerr << "usage: <transport> <size>" << std::endl;
        return 1;
    }

    const std::string transport = argv[1];
    const size_t size = static_cast<size_t> (std::strtoull (argv[2], NULL, 10));
    if (size == 0)
        return 1;

    perf_pubsub_server (transport, size);
    return 0;
}
