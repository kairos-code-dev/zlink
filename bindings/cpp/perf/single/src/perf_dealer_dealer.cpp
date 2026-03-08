// DEALER-DEALER benchmark: one-way sender->receiver loop.
// Topology: sender(DEALER connect) -> receiver(DEALER bind)

#include "../common/perf_single_common.hpp"
#include "../common/perf_single_runner.hpp"

#include <algorithm>
#include <cerrno>
#include <vector>

namespace {

bool send_single_payload_once (zlink::socket_t &socket,
                               const void *payload,
                               size_t payload_size)
{
    return socket.send (payload, payload_size, zlink::send_flag::none)
           == static_cast<int> (payload_size);
}

int recv_header_single_part (zlink::socket_t &socket,
                             std::vector<char> &recv_buffer,
                             size_t expected_size,
                             zlink::recv_flag flags,
                             perf_single_metric::header_t *header_out,
                             bool *header_ok_out)
{
    if (header_ok_out)
        *header_ok_out = false;

    const int rc = socket.recv (recv_buffer.data (), recv_buffer.size (), flags);
    if (rc < 0) {
        const int err = errno;
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }

    int more = 0;
    if (socket.get (zlink::socket_options::rcvmore, more) != 0
        || more != 0
        || static_cast<size_t> (rc) != expected_size) {
        return -1;
    }

    bool header_ok = false;
    if (header_out) {
        header_ok = perf_single_metric::decode_payload_header (
          recv_buffer.data (), static_cast<size_t> (rc), header_out);
    }

    if (header_ok_out)
        *header_ok_out = header_ok;
    return 1;
}

bool drain_recv_queue (zlink::socket_t &socket,
                       std::vector<char> &recv_buffer,
                       size_t payload_size,
                       uint32_t run_id,
                       perf_single_metric::phase_t phase,
                       size_t msg_size,
                       bool active,
                       unsigned long long *received,
                       perf::single::latency_stats_builder_t *latency_builder)
{
    for (;;) {
        perf_single_metric::header_t header;
        bool header_ok = false;
        const int recv_rc = recv_header_single_part (
          socket,
          recv_buffer,
          payload_size,
          zlink::recv_flag::dontwait,
          &header,
          &header_ok);
        if (recv_rc == 0)
            return true;
        if (recv_rc < 0)
            return false;
        if (!header_ok || !perf_single_metric::is_expected (
                            header, run_id, phase, msg_size)) {
            continue;
        }

        ++(*received);
        if (active && latency_builder) {
            const uint64_t now = perf_single_metric::now_us ();
            const double latency_us =
              now >= header.sent_ts_us
                ? static_cast<double> (now - header.sent_ts_us)
                : 0.0;
            latency_builder->add (latency_us);
        }
    }
}

bool run_phase (zlink::socket_t &sender,
                zlink::socket_t &receiver,
                std::vector<char> &payload,
                size_t msg_size,
                uint32_t run_id,
                uint64_t &seq,
                perf_single_metric::phase_t phase,
                int warmup_count,
                int duration_s,
                perf::single::queue_probe_t *queue_probe,
                unsigned long long *received_out,
                perf::single::latency_stats_t *latency_out)
{
    if (!received_out)
        return false;

    const size_t payload_size = payload.size ();
    const bool active = phase == perf_single_metric::phase_active;
    std::vector<char> recv_buffer (payload_size);

    perf::single::latency_stats_builder_t latency_builder (
      perf::single::resolve_single_latency_sample_cap ());
    unsigned long long received = 0;

    auto do_one = [&] () -> bool {
        const uint64_t sent_ts = perf_single_metric::now_us ();
        if (!perf_single_metric::stamp_payload (payload.data (),
                                                payload_size,
                                                run_id,
                                                phase,
                                                msg_size,
                                                seq++,
                                                sent_ts)
            || !send_single_payload_once (sender, payload.data (), payload_size)) {
            return false;
        }
        if (queue_probe)
            queue_probe->sample_send_if_due ();

        perf_single_metric::header_t header;
        bool header_ok = false;
        const int recv_rc = recv_header_single_part (
          receiver,
          recv_buffer,
          payload_size,
          zlink::recv_flag::none,
          &header,
          &header_ok);
        if (recv_rc <= 0)
            return false;
        if (queue_probe)
            queue_probe->sample_recv_if_due ();

        if (header_ok && perf_single_metric::is_expected (
                           header, run_id, phase, msg_size)) {
            ++received;
            if (active) {
                const uint64_t now = perf_single_metric::now_us ();
                const double latency_us =
                  now >= header.sent_ts_us
                    ? static_cast<double> (now - header.sent_ts_us)
                    : 0.0;
                latency_builder.add (latency_us);
            }
        }

        return drain_recv_queue (receiver,
                                 recv_buffer,
                                 payload_size,
                                 run_id,
                                 phase,
                                 msg_size,
                                 active,
                                 &received,
                                 active ? &latency_builder : NULL);
    };

    if (active) {
        const auto deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::seconds (duration_s > 0 ? duration_s : 1);
        while (std::chrono::steady_clock::now () < deadline) {
            if (!do_one ())
                return false;
        }
    } else {
        for (int i = 0; i < warmup_count; ++i) {
            if (!do_one ())
                return false;
        }
    }

    if (queue_probe) {
        queue_probe->force_sample_send ();
        queue_probe->force_sample_recv ();
    }

    if (active) {
        if (received == 0 || latency_builder.count () == 0 || !latency_out)
            return false;
        *latency_out = latency_builder.snapshot ();
    }

    *received_out = received;
    return true;
}

} // namespace

void run_pattern_dealer_dealer (const std::string &transport,
                                size_t msg_size,
                                const std::string &lib_name)
{
    if (!perf::single::transport_available (transport)) {
        std::cout << "UNSUPPORTED,DEALER_DEALER," << transport << std::endl;
        return;
    }

    perf::single::ctx_guard_t ctx;
    if (!ctx.valid ()) {
        perf::single::print_fail_result (
          lib_name, "DEALER_DEALER", transport, msg_size);
        return;
    }

    perf::single::socket_guard_t bind_socket (ctx, zlink::socket_type::dealer);
    perf::single::socket_guard_t conn_socket (ctx, zlink::socket_type::dealer);
    if (!bind_socket.valid () || !conn_socket.valid ()) {
        perf::single::print_fail_result (
          lib_name, "DEALER_DEALER", transport, msg_size);
        return;
    }

    if (!perf::single::setup_connected_pair (bind_socket.sock (),
                                             conn_socket.sock (),
                                             transport,
                                             lib_name + "_dealer_dealer")) {
        perf::single::print_fail_result (
          lib_name, "DEALER_DEALER", transport, msg_size);
        return;
    }

    const int recv_timeout = perf::single::resolve_single_recv_timeout_ms ();
    (void) bind_socket.sock ().set (zlink::socket_options::rcvtimeo, recv_timeout);
    (void) conn_socket.sock ().set (zlink::socket_options::rcvtimeo, recv_timeout);

    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 'a');

    perf::single::queue_probe_t queue_probe (&conn_socket.sock (),
                                             &bind_socket.sock ());

    const uint32_t run_id = static_cast<uint32_t> (perf_single_metric::now_us ());
    uint64_t seq = 1;

    const int warmup_count =
      perf::single::resolve_bench_count ("PERF_WARMUP_COUNT", 1000);
    unsigned long long warmup_received = 0;
    if (!run_phase (conn_socket.sock (),
                    bind_socket.sock (),
                    payload,
                    msg_size,
                    run_id,
                    seq,
                    perf_single_metric::phase_warmup,
                    warmup_count,
                    0,
                    NULL,
                    &warmup_received,
                    NULL)) {
        perf::single::print_fail_result (
          lib_name, "DEALER_DEALER", transport, msg_size, &queue_probe);
        return;
    }

    const int duration_s = std::max (1, perf::single::resolve_single_duration_seconds ());
    unsigned long long received = 0;
    perf::single::latency_stats_t latency;
    if (!run_phase (conn_socket.sock (),
                    bind_socket.sock (),
                    payload,
                    msg_size,
                    run_id,
                    seq,
                    perf_single_metric::phase_active,
                    0,
                    duration_s,
                    &queue_probe,
                    &received,
                    &latency)) {
        perf::single::print_fail_result (
          lib_name, "DEALER_DEALER", transport, msg_size, &queue_probe);
        return;
    }

    const double throughput =
      static_cast<double> (received) / static_cast<double> (duration_s);
    perf::single::print_result (lib_name,
                                "DEALER_DEALER",
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
    return perf::single::run_standard_bench_main (argc, argv, run_pattern_dealer_dealer);
}
