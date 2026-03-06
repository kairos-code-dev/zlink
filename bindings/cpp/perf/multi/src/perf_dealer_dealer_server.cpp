// DEALER-DEALER multi server benchmark: one-way receive sink.
// Topology: client DEALER(connect, N) -> server DEALER(bind, 1)
// Measurement role: drain incoming payloads and emit server queue metrics.

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"

#include <cerrno>
#include <chrono>
#include <vector>

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

    zlink::poller_t poller;
    (void) poller.add (server.sock (), zlink::poll_event::pollin);
    std::vector<zlink::poll_event_t> events;
    events.reserve (1);

    bool stop_requested = false;
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

        const int poll_rc = poller.wait (events, wait_ms);
        if (poll_rc < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (poll_rc == 0)
            continue;

        for (size_t i = 0; i < events.size () && !stop_requested; ++i) {
            zlink::socket_t *sock = events[i].socket;
            if (!sock)
                continue;

            for (;;) {
                zlink::message_t msg;
                const int rc = sock->recv (msg, zlink::recv_flag::dontwait);
                if (rc < 0) {
                    const int err = errno;
                    if (err == EAGAIN)
                        break;
                    if (err == EINTR)
                        continue;
                    stop_requested = true;
                    break;
                }

                if (perf::multi::is_stop_token_message (msg)) {
                    stop_requested = true;
                    break;
                }
            }
        }
    }

    perf::multi::print_server_queue_metrics (
      "current",
      "MULTI_DEALER_DEALER",
      transport,
      0,
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

    perf_dealer_dealer_server (transport, size);
    return 0;
}
