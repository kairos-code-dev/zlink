// ROUTER-ROUTER benchmark: one-way router->router with explicit handshake.

#include "../common/perf_single_common.hpp"
#include "../common/perf_single_runner.hpp"

#include <atomic>
#include <thread>
#include <vector>

namespace {

typedef std::chrono::steady_clock steady_clock_t;

const char *const k_receiver_id = "ROUTER1";
const char *const k_sender_id = "ROUTER2";

bool complete_handshake (zlink::socket_t &receiver, zlink::socket_t &sender)
{
    char buf[16];
    return sender.send (k_receiver_id,
                        std::strlen (k_receiver_id),
                        zlink::send_flag::sndmore)
             == static_cast<int> (std::strlen (k_receiver_id))
           && sender.send ("PING", 4) == 4
           && receiver.recv (buf, std::strlen (k_receiver_id)) == 7
           && receiver.recv (buf, 4) == 4
           && receiver.send (k_sender_id,
                             std::strlen (k_sender_id),
                             zlink::send_flag::sndmore)
                == static_cast<int> (std::strlen (k_sender_id))
           && receiver.send ("PONG", 4) == 4
           && sender.recv (buf, std::strlen (k_sender_id)) == 7
           && sender.recv (buf, 4) == 4;
}

int recv_router_header (zlink::socket_t &router,
                        size_t payload_size,
                        zlink::recv_flag flags,
                        perf_single_metric::header_t *header_out,
                        bool *header_ok_out)
{
    if (header_ok_out)
        *header_ok_out = false;

    zlink::message_t routing_id;
    const int id_rc = router.recv (routing_id, flags);
    if (id_rc < 0) {
        const int err = errno;
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }

    if (!routing_id.more ())
        return -1;

    zlink::message_t payload;
    if (router.recv (payload, zlink::recv_flag::none) < 0)
        return -1;

    if (payload.more () || payload.size () != payload_size)
        return -1;

    bool header_ok = false;
    if (header_out) {
        header_ok = perf_single_metric::decode_payload_header (
          payload.data (), payload.size (), header_out);
    }

    if (header_ok_out)
        *header_ok_out = header_ok;
    return 1;
}

bool run_phase (zlink::socket_t &receiver,
                zlink::socket_t &sender,
                std::vector<char> &payload,
                size_t msg_size,
                uint32_t run_id,
                uint64_t &seq,
                perf_single_metric::phase_t phase,
                int warmup_count,
                int duration_s,
                int recv_timeout_ms,
                perf::single::queue_probe_t *queue_probe,
                unsigned long long *received_out,
                perf::single::latency_stats_t *latency_out)
{
    if (!received_out)
        return false;

    const bool active = phase == perf_single_metric::phase_active;
    const auto deadline =
      active ? steady_clock_t::now ()
                   + std::chrono::seconds (duration_s > 0 ? duration_s : 1)
             : steady_clock_t::time_point ();
    const auto drain_idle_limit =
      std::chrono::milliseconds (recv_timeout_ms > 0 ? recv_timeout_ms : 200);

    perf::single::latency_stats_builder_t latency_builder (
      perf::single::resolve_single_latency_sample_cap ());
    std::atomic<bool> sender_done (false);
    std::atomic<bool> recv_failed (false);
    unsigned long long received = 0;

    std::thread receiver_thread ([&] () {
        auto last_recv_at = steady_clock_t::now ();

        auto account_header =
          [&] (const perf_single_metric::header_t &header, bool header_ok) {
              if (active && queue_probe)
                  queue_probe->sample_recv_if_due ();

              if (!header_ok
                  || !perf_single_metric::is_expected (
                    header, run_id, phase, msg_size)) {
                  return;
              }

              ++received;
              if (!active)
                  return;

              const uint64_t now = perf_single_metric::now_us ();
              const double latency_us =
                now >= header.sent_ts_us
                  ? static_cast<double> (now - header.sent_ts_us)
                  : 0.0;
              latency_builder.add (latency_us);
          };

        if (active && queue_probe)
            queue_probe->force_sample_recv ();

        while (true) {
            const bool done = sender_done.load (std::memory_order_acquire);
            const zlink::recv_flag flags =
              done ? zlink::recv_flag::dontwait : zlink::recv_flag::none;

            perf_single_metric::header_t header;
            bool header_ok = false;
            int recv_rc =
              recv_router_header (receiver, payload.size (), flags, &header, &header_ok);
            if (recv_rc > 0) {
                last_recv_at = steady_clock_t::now ();
                account_header (header, header_ok);

                for (;;) {
                    perf_single_metric::header_t burst_header;
                    bool burst_ok = false;
                    recv_rc = recv_router_header (receiver,
                                                  payload.size (),
                                                  zlink::recv_flag::dontwait,
                                                  &burst_header,
                                                  &burst_ok);
                    if (recv_rc > 0) {
                        last_recv_at = steady_clock_t::now ();
                        account_header (burst_header, burst_ok);
                        continue;
                    }
                    if (recv_rc == 0)
                        break;

                    recv_failed.store (true, std::memory_order_release);
                    break;
                }

                if (recv_failed.load (std::memory_order_acquire))
                    break;
                continue;
            }

            if (recv_rc == 0) {
                if (done && steady_clock_t::now () - last_recv_at >= drain_idle_limit)
                    break;
                std::this_thread::yield ();
                continue;
            }

            recv_failed.store (true, std::memory_order_release);
            break;
        }

        if (active && queue_probe)
            queue_probe->force_sample_recv ();
    });

    bool send_failed = false;
    if (active && queue_probe)
        queue_probe->force_sample_send ();

    auto send_one = [&] (uint64_t sent_ts) -> bool {
        return perf_single_metric::stamp_payload (payload.data (),
                                                  payload.size (),
                                                  run_id,
                                                  phase,
                                                  msg_size,
                                                  seq++,
                                                  sent_ts)
               && sender.send (k_receiver_id,
                               std::strlen (k_receiver_id),
                               zlink::send_flag::sndmore)
                    == static_cast<int> (std::strlen (k_receiver_id))
               && sender.send (
                    payload.data (), payload.size (), zlink::send_flag::none)
                    == static_cast<int> (payload.size ());
    };

    if (active) {
        while (steady_clock_t::now () < deadline) {
            if (!send_one (perf_single_metric::now_us ())) {
                send_failed = true;
                break;
            }
            if (queue_probe)
                queue_probe->sample_send_if_due ();
        }
    } else {
        for (int i = 0; i < warmup_count; ++i) {
            if (!send_one (perf_single_metric::now_us ())) {
                send_failed = true;
                break;
            }
        }
    }

    if (active && queue_probe)
        queue_probe->force_sample_send ();

    sender_done.store (true, std::memory_order_release);
    receiver_thread.join ();

    if (send_failed || recv_failed.load (std::memory_order_acquire))
        return false;

    *received_out = received;
    if (active) {
        if (received == 0 || latency_builder.count () == 0 || !latency_out)
            return false;
        *latency_out = latency_builder.snapshot ();
    } else if (received < static_cast<unsigned long long> (warmup_count)) {
        return false;
    }

    return true;
}

} // namespace

void run_pattern_router_router (const std::string &transport,
                                size_t msg_size,
                                const std::string &lib_name)
{
    if (!perf::single::transport_available (transport)) {
        std::cout << "UNSUPPORTED,ROUTER_ROUTER," << transport << std::endl;
        return;
    }

    perf::single::ctx_guard_t ctx;
    if (!ctx.valid ()) {
        perf::single::print_fail_result (
          lib_name, "ROUTER_ROUTER", transport, msg_size);
        return;
    }

    perf::single::socket_guard_t receiver (ctx, zlink::socket_type::router);
    perf::single::socket_guard_t sender (ctx, zlink::socket_type::router);
    if (!receiver.valid () || !sender.valid ()) {
        perf::single::print_fail_result (
          lib_name, "ROUTER_ROUTER", transport, msg_size);
        return;
    }

    (void) receiver.sock ().set (zlink::socket_options::routing_id,
                                 std::string (k_receiver_id));
    (void) sender.sock ().set (zlink::socket_options::routing_id,
                               std::string (k_sender_id));
    (void) receiver.sock ().set (zlink::socket_options::router_mandatory, 1);
    (void) sender.sock ().set (zlink::socket_options::router_mandatory, 1);

    if (!perf::single::setup_connected_pair (receiver.sock (),
                                             sender.sock (),
                                             transport,
                                             lib_name + "_router_router")
        || !complete_handshake (receiver.sock (), sender.sock ())) {
        perf::single::print_fail_result (
          lib_name, "ROUTER_ROUTER", transport, msg_size);
        return;
    }

    const int recv_timeout = perf::single::resolve_single_recv_timeout_ms ();
    (void) receiver.sock ().set (zlink::socket_options::rcvtimeo, recv_timeout);
    (void) sender.sock ().set (zlink::socket_options::rcvtimeo, recv_timeout);

    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 'a');

    perf::single::queue_probe_t queue_probe (&sender.sock (), &receiver.sock ());

    const uint32_t run_id = static_cast<uint32_t> (perf_single_metric::now_us ());
    uint64_t seq = 1;

    const int warmup_count =
      perf::single::resolve_bench_count ("PERF_WARMUP_COUNT", 1000);
    unsigned long long warmup_received = 0;
    if (!run_phase (receiver.sock (),
                    sender.sock (),
                    payload,
                    msg_size,
                    run_id,
                    seq,
                    perf_single_metric::phase_warmup,
                    warmup_count,
                    0,
                    recv_timeout,
                    NULL,
                    &warmup_received,
                    NULL)) {
        perf::single::print_fail_result (
          lib_name, "ROUTER_ROUTER", transport, msg_size, &queue_probe);
        return;
    }

    const int duration_s = std::max (1, perf::single::resolve_single_duration_seconds ());
    unsigned long long received = 0;
    perf::single::latency_stats_t latency;
    if (!run_phase (receiver.sock (),
                    sender.sock (),
                    payload,
                    msg_size,
                    run_id,
                    seq,
                    perf_single_metric::phase_active,
                    0,
                    duration_s,
                    recv_timeout,
                    &queue_probe,
                    &received,
                    &latency)) {
        perf::single::print_fail_result (
          lib_name, "ROUTER_ROUTER", transport, msg_size, &queue_probe);
        return;
    }

    const double throughput =
      static_cast<double> (received) / static_cast<double> (duration_s);
    perf::single::print_result (lib_name,
                                "ROUTER_ROUTER",
                                transport,
                                msg_size,
                                throughput,
                                latency.mean_us,
                                latency.p95_us,
                                latency.p99_us,
                                queue_probe.snapshot ());
}

int main (int argc, char **argv)
{
    return perf::single::run_standard_bench_main (argc, argv, run_pattern_router_router);
}
