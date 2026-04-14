// DEALER-DEALER benchmark: one-way sender->receiver loop inside one process.

#include "../common/perf_single_common.hpp"
#include "../common/perf_single_runner.hpp"

#include <atomic>
#include <thread>
#include <vector>

namespace {

bool send_single_part (void *userdata_, const void *data_, size_t size_)
{
    zlink::socket_t *socket = static_cast<zlink::socket_t *> (userdata_);
    if (!socket)
        return false;

    zlink::message_t msg = zlink::message_t::from_bytes (data_, size_);
    return msg.valid () && socket->send (msg, zlink::send_flags_t::none) == 0;
}

bool record_dealer_payload (const zlink::received_t &received,
                            uint32_t run_id,
                            size_t msg_size,
                            size_t payload_size,
                            std::atomic<unsigned long long> &received_count,
                            perf::single::latency_stats_builder_t &latency_builder)
{
    const zlink::message_t *payload = NULL;
    if (received.parts ().size () == 1) {
        payload = &received.parts ()[0];
    } else if (received.parts ().size () == 2
               && received.parts ()[0].size () == 0) {
        payload = &received.parts ()[1];
    }
    if (!payload || payload->size () != payload_size)
        return true;

    perf_single_metric::header_t header;
    if (!perf_single_metric::decode_payload_header (
          payload->data (), payload->size (), &header)) {
        return true;
    }

    if (!perf_single_metric::is_expected (
          header, run_id, perf_single_metric::phase_active, msg_size)) {
        return true;
    }

    received_count.fetch_add (1, std::memory_order_release);
    const uint64_t now = perf_single_metric::now_ns ();
    latency_builder.add (
      now >= header.sent_ts_ns ? static_cast<double> (now - header.sent_ts_ns)
                               : 0.0);
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

    (void) bind_socket.sock ().set_option (zlink::socket_options::tcp_nodelay, 1);
    (void) conn_socket.sock ().set_option (zlink::socket_options::tcp_nodelay, 1);

    if (!perf::single::setup_connected_pair (bind_socket.sock (),
                                             conn_socket.sock (),
                                             transport,
                                             lib_name + "_dealer_dealer")) {
        perf::single::print_fail_result (
          lib_name, "DEALER_DEALER", transport, msg_size);
        return;
    }

    const int recv_timeout = perf::single::resolve_single_recv_timeout_ms ();
    (void) bind_socket.sock ().set_option (
      zlink::socket_options::rcvtimeo, recv_timeout);
    (void) conn_socket.sock ().set_option (
      zlink::socket_options::sndtimeo, perf::single::resolve_single_send_timeout_ms ());

    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 'a');

    const uint32_t run_id = static_cast<uint32_t> (perf_single_metric::now_ns ());
    const int duration_s =
      std::max (1, perf::single::resolve_single_duration_seconds ());
    std::atomic<unsigned long long> sent_count (0);
    std::atomic<unsigned long long> received_count (0);
    std::atomic<bool> sender_ok (true);
    std::atomic<bool> sender_done (false);
    perf::single::latency_stats_builder_t latency_builder (
      perf::single::resolve_single_latency_sample_cap ());

    std::thread sender_thread ([&]() {
        uint64_t seq = 1;
        const auto deadline =
          std::chrono::steady_clock::now () + std::chrono::seconds (duration_s);
        while (std::chrono::steady_clock::now () < deadline) {
            if (!perf_single_metric::stamp_payload (payload.data (),
                                                    payload.size (),
                                                    run_id,
                                                    perf_single_metric::phase_active,
                                                    msg_size,
                                                    seq++,
                                                    perf_single_metric::now_ns ())
                || !send_single_part (
                  &conn_socket.sock (), payload.data (), payload.size ())) {
                sender_ok.store (false, std::memory_order_release);
                break;
            }
            sent_count.fetch_add (1, std::memory_order_release);
        }
        sender_done.store (true, std::memory_order_release);
    });

    zlink::poller_t poller;
    poller.add (bind_socket.sock (), zlink::poll_event::pollin);
    while (!sender_done.load (std::memory_order_acquire)) {
        zlink::poll_event_t event = {};
        const int poll_rc = poller.wait (&event, 5);
        if (poll_rc < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            sender_ok.store (false, std::memory_order_release);
            break;
        }
        if (poll_rc == 0
            || (event.revents & static_cast<short> (zlink::poll_event::pollin))
                 == 0) {
            continue;
        }

        for (;;) {
            zlink::received_t received;
            const int recv_rc =
              bind_socket.sock ().receive (received, zlink::recv_flags_t::dontwait);
            if (recv_rc != 0) {
                if (errno == EAGAIN || errno == EINTR)
                    break;
                sender_ok.store (false, std::memory_order_release);
                break;
            }
            if (!record_dealer_payload (received,
                                        run_id,
                                        msg_size,
                                        payload_size,
                                        received_count,
                                        latency_builder)) {
                sender_ok.store (false, std::memory_order_release);
                break;
            }
        }
    }

    sender_thread.join ();
    const auto drain_deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (recv_timeout * 2);
    while (received_count.load (std::memory_order_acquire)
             < sent_count.load (std::memory_order_acquire)
           && std::chrono::steady_clock::now () < drain_deadline) {
        zlink::received_t received;
        const int recv_rc =
          bind_socket.sock ().receive (received, zlink::recv_flags_t::dontwait);
        if (recv_rc != 0) {
            if (errno == EAGAIN || errno == EINTR)
                break;
            sender_ok.store (false, std::memory_order_release);
            break;
        }
        if (!record_dealer_payload (received,
                                    run_id,
                                    msg_size,
                                    payload_size,
                                    received_count,
                                    latency_builder)) {
            sender_ok.store (false, std::memory_order_release);
            break;
        }
    }

    const unsigned long long received =
      received_count.load (std::memory_order_acquire);
    if (!sender_ok.load (std::memory_order_acquire) || received == 0
        || latency_builder.count () == 0) {
        perf::single::print_fail_result (
          lib_name, "DEALER_DEALER", transport, msg_size);
        return;
    }
    const perf::single::latency_stats_t latency = latency_builder.snapshot ();

    const double throughput =
      static_cast<double> (received) / static_cast<double> (duration_s);
    perf::single::print_result (lib_name,
                                "DEALER_DEALER",
                                transport,
                                msg_size,
                                throughput,
                                latency.mean_ns,
                                latency.p95_ns,
                                latency.p99_ns);
}

int main (int argc, char **argv)
{
    return perf::single::run_standard_bench_main (
      argc, argv, run_pattern_dealer_dealer);
}
