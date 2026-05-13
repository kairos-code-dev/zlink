// DEALER-DEALER benchmark: one-way sender->receiver loop inside one process.

#include "../common/perf_single_common.hpp"
#include "../common/perf_single_runner.hpp"

#include <atomic>
#include <thread>
#include <vector>

namespace {

bool send_single_part (void *userdata_, const void *data_, size_t size_)
{
    ::perf::socket_t *socket = static_cast<::perf::socket_t *> (userdata_);
    if (!socket)
        return false;

    zlink::message_t msg = zlink::message_t::from_bytes (data_, size_);
    return msg.valid ()
           && perf::send_socket (*socket, msg, ZLINK_DONTWAIT) == 0;
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

bool run_pattern_dealer_dealer (const std::string &transport,
                                size_t msg_size,
                                const std::string &lib_name)
{
    if (!perf::single::transport_available (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << ",DEALER_DEALER,"
                  << transport << std::endl;
        return true;
    }

    perf::single::ctx_guard_t ctx;
    if (!ctx.valid ()) {
        return false;
    }

    perf::single::socket_guard_t bind_socket (ctx, zlink::socket_type::dealer);
    perf::single::socket_guard_t conn_socket (ctx, zlink::socket_type::dealer);
    if (!bind_socket.valid () || !conn_socket.valid ()) {
        return false;
    }

    (void) bind_socket.sock ().set_option (zlink::compat::options::socket_options::tcp_nodelay, 1);
    (void) conn_socket.sock ().set_option (zlink::compat::options::socket_options::tcp_nodelay, 1);
    if (!perf::single::apply_single_auto_hwm_msg_unit (
          bind_socket.sock (), msg_size)
        || !perf::single::apply_single_auto_hwm_msg_unit (
          conn_socket.sock (), msg_size)
        || !perf::single::recalculate_single_auto_hwm (ctx)) {
        return false;
    }

    if (!perf::single::setup_connected_pair (bind_socket.sock (),
                                             conn_socket.sock (),
                                             transport,
                                             lib_name + "_dealer_dealer")) {
        return false;
    }

    const int recv_timeout = perf::single::resolve_single_recv_timeout_ms ();
    (void) bind_socket.sock ().set_option (
      zlink::compat::options::socket_options::rcvtimeo, recv_timeout);
    (void) conn_socket.sock ().set_option (
      zlink::compat::options::socket_options::sndtimeo, perf::single::resolve_single_send_timeout_ms ());

    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 'a');

    const uint32_t run_id = 1U;
    const int duration_s =
      std::max (1, perf::single::resolve_single_duration_seconds ());
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
                                                    perf_single_metric::now_ns ())
                || !send_single_part (
                  &conn_socket.sock (), payload.data (), payload.size ())) {
                if (errno == EAGAIN || errno == EWOULDBLOCK
                    || errno == ETIMEDOUT || errno == EINTR) {
                    continue;
                }
                sender_ok.store (false, std::memory_order_release);
                break;
            }
            sent_count.fetch_add (1, std::memory_order_release);
        }
        // PERF_SINGLE_TEST_POLICY § 1.4: signal phase end via wire-level
        // stop token. Blocking send retries through transient backpressure
        // so the receiver always observes the terminator.
        zlink::message_t stop_msg = zlink::message_t::from_bytes (
          perf::single::k_stop_token,
          std::strlen (perf::single::k_stop_token));
        for (int retry = 0; retry < 100 && stop_msg.valid (); ++retry) {
            try {
                if (perf::send_socket (conn_socket.sock (), stop_msg, 0) == 0)
                    break;
            }
            catch (const zlink::zlink_error_t &) {
                break;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
    });

    zlink::poller_t poller;
    bind_socket.sock ().poller_add (poller, zlink::poll_event_flag_t::pollin);
    bool stop_received = false;
    while (!stop_received) {
        // PERF_SINGLE_TEST_POLICY § 1.4: signal-driven wait, no timer cap.
        std::optional<zlink::poll_event_t> event =
          poller.wait (std::chrono::milliseconds (-1));
        if (!event) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            sender_ok.store (false, std::memory_order_release);
            break;
        }
        if ((static_cast<short> (event->revents)
             & static_cast<short> (zlink::poll_event_flag_t::pollin))
            == 0) {
            continue;
        }

        for (;;) {
            zlink::received_t received;
            const int recv_rc =
              bind_socket.sock ().receive (received, ZLINK_DONTWAIT);
            if (recv_rc != 0) {
                if (errno == EAGAIN || errno == EINTR)
                    break;
                sender_ok.store (false, std::memory_order_release);
                break;
            }
            if (received.parts ().size () == 1
                && perf::single::is_stop_token_message (received.parts ()[0])) {
                stop_received = true;
                break;
            }
            if (std::chrono::steady_clock::now () < active_deadline
                && !record_dealer_payload (received,
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
    // Stop token is the last in-flight message, so any earlier payloads
    // have already been recorded above. No bounded drain loop needed.
    const auto drain_deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (0);
    while (received_count.load (std::memory_order_acquire)
             < sent_count.load (std::memory_order_acquire)
           && std::chrono::steady_clock::now () < drain_deadline) {
        zlink::received_t received;
        const int recv_rc =
          bind_socket.sock ().receive (received, ZLINK_DONTWAIT);
        if (recv_rc != 0) {
            if (errno == EAGAIN || errno == EINTR)
                break;
            sender_ok.store (false, std::memory_order_release);
            break;
        }
        if (std::chrono::steady_clock::now () < active_deadline
            && !record_dealer_payload (received,
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
        return false;
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
    return true;
}

int main (int argc, char **argv)
{
    return perf::single::run_standard_bench_main (
      argc, argv, run_pattern_dealer_dealer);
}
