// DEALER-ROUTER benchmark: one-way dealer->router.

#include "../common/perf_single_common.hpp"
#include "../common/perf_single_runner.hpp"

#include <thread>
#include <vector>

namespace {

bool perf_debug_enabled ()
{
    return std::getenv ("PERF_DEBUG") != NULL;
}

} // namespace

bool run_pattern_dealer_router (const std::string &transport,
                                size_t msg_size,
                                const std::string &lib_name)
{
    if (!perf::single::transport_available (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << ",DEALER_ROUTER,"
                  << transport << std::endl;
        return true;
    }

    perf::single::ctx_guard_t ctx;
    if (!ctx.valid ()) {
        return false;
    }

    perf::single::socket_guard_t router (ctx, zlink::socket_type::router);
    perf::single::socket_guard_t dealer (ctx, zlink::socket_type::dealer);
    if (!router.valid () || !dealer.valid ()) {
        return false;
    }

    (void) dealer.sock ().set_routing_id (std::string ("CLIENT"));
    if (!perf::single::apply_single_auto_hwm_msg_unit (router.sock (), msg_size)
        || !perf::single::apply_single_auto_hwm_msg_unit (
          dealer.sock (), msg_size)
        || !perf::single::recalculate_single_auto_hwm (ctx)) {
        return false;
    }
    if (!perf::single::setup_connected_pair (router.sock (),
                                             dealer.sock (),
                                             transport,
                                             lib_name + "_dealer_router")) {
        return false;
    }

    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 'a');

    const uint32_t run_id = 1U;
    const int duration_s =
      std::max (1, perf::single::resolve_single_duration_seconds ());
    const int recv_timeout = perf::single::resolve_single_recv_timeout_ms ();
    std::atomic<unsigned long long> sent_count (0);
    std::atomic<unsigned long long> received_count (0);
    std::atomic<bool> sender_ok (true);
    perf::single::latency_stats_builder_t latency_builder (
      perf::single::resolve_single_latency_sample_cap ());
    const auto active_deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (duration_s);

    std::thread sender_thread ([&]() {
        uint64_t seq = 1;
        while (std::chrono::steady_clock::now () < active_deadline) {
            if (!perf_single_metric::stamp_payload (payload.data (),
                                                    payload.size (),
                                                    run_id,
                                                    perf_single_metric::phase_active,
                                                    msg_size,
                                                    seq++,
                                                    perf_single_metric::now_ns ())) {
                sender_ok.store (false, std::memory_order_release);
                break;
            }

            if (!perf::single::send_payload_blocking (
                  dealer.sock (), payload.data (), payload.size ())) {
                const int err = errno;
                if (perf::single::is_transient_send_errno (err)
                    && std::chrono::steady_clock::now () < active_deadline) {
                    continue;
                }
                if (perf::single::is_transient_send_errno (err))
                    break;
                if (perf_debug_enabled ())
                    std::cerr << "dealer_router: send failed errno=" << err
                              << std::endl;
                sender_ok.store (false, std::memory_order_release);
                break;
            }
            sent_count.fetch_add (1, std::memory_order_release);
        }
        // PERF_SINGLE_TEST_POLICY § 1.4: signal phase end with one
        // wire-level blocking stop token.
        if (!perf::single::send_stop_token_blocking (dealer.sock ()))
            sender_ok.store (false, std::memory_order_release);
    });

    // C-faithful receiver (bindings/c/perf single perf_dealer_router.cpp
    // run_active_phase): blocking recv (flags=0, bounded by rcvtimeo) into
    // a single reused routing_id_t + message_t, exiting on the wire-level
    // stop token. The previous poller.wait()+received_t drain allocated a
    // fresh std::vector<message_t> per message (received_t::parts ()
    // materialize), capping DEALER_ROUTER throughput at ~70% of C; C uses
    // one reused zlink_msg_t recv buffer with no per-message heap churn.
    {
        zlink::routing_id_t source_rid =
          zlink::detail::unchecked_empty_routing_id ();
        bool stop_received = false;
        while (!stop_received) {
            zlink::message_t part;
            const int recv_rc =
              router.sock ().recv (source_rid, part, 0);
            if (recv_rc != 0) {
                if (errno == EAGAIN || errno == EINTR)
                    continue;
                if (perf_debug_enabled ())
                    std::cerr << "dealer_router: recv failed errno="
                              << errno << std::endl;
                sender_ok.store (false, std::memory_order_release);
                break;
            }
            if (perf::single::is_stop_token_message (part)) {
                stop_received = true;
                break;
            }
            if (part.size () != payload_size)
                continue;
            perf_single_metric::header_t header;
            if (!perf_single_metric::decode_payload_header (
                  part.data (), part.size (), &header))
                continue;
            if (!perf_single_metric::is_expected (
                  header, run_id, perf_single_metric::phase_active,
                  msg_size))
                continue;
            if (std::chrono::steady_clock::now () < active_deadline) {
                received_count.fetch_add (1, std::memory_order_release);
                const uint64_t now = perf_single_metric::now_ns ();
                latency_builder.add (
                  perf_single_metric::elapsed_latency_ns (
                    now, header.sent_ts_ns));
            }
        }
    }

    sender_thread.join ();
    (void) recv_timeout;

    const unsigned long long received =
      received_count.load (std::memory_order_acquire);
    if (!sender_ok.load (std::memory_order_acquire) || received == 0
        || latency_builder.count () == 0) {
        if (perf_debug_enabled ())
            std::cerr << "dealer_router: no active data sent="
                      << sent_count.load (std::memory_order_acquire)
                      << " received=" << received << std::endl;
        return false;
    }

    const perf::single::latency_stats_t latency = latency_builder.snapshot ();

    perf::single::emit_single_socket_hwm_detail (
      router.sock (), "DEALER_ROUTER", transport, "receiver", "router",
      msg_size);
    perf::single::emit_single_socket_hwm_detail (
      dealer.sock (), "DEALER_ROUTER", transport, "sender", "dealer",
      msg_size);

    const double throughput =
      static_cast<double> (received) / static_cast<double> (duration_s);
    perf::single::print_result (lib_name,
                                "DEALER_ROUTER",
                                transport,
                                msg_size,
                                throughput,
                                latency.mean_ns,
                                latency.p95_ns,
                                latency.p99_ns);
    return true;
}

int main (int argc, char **argv)
{
    return perf::single::run_standard_bench_main (
      argc, argv, run_pattern_dealer_router);
}
