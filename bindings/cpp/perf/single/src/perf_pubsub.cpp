// PUBSUB benchmark: one-way publisher->subscriber loop.
// Topology: publisher(PUB bind) -> subscriber(SUB connect)

#include "../common/perf_single_common.hpp"
#include "../common/perf_single_runner.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace {

const char *const k_topic = "bench";

bool perf_debug_enabled ()
{
    return std::getenv ("PERF_DEBUG") != NULL;
}

bool send_pubsub_payload (void *userdata_, const void *data_, size_t size_)
{
    zlink::socket_t *publisher = static_cast<zlink::socket_t *> (userdata_);
    if (!publisher)
        return false;

    zlink::message_t part = zlink::message_t::from_bytes (data_, size_);
    return part.valid () && publisher->publish (k_topic, part) == 0;
}

bool record_subscribed_payload (
  const zlink::topic_message_t &message,
  uint32_t run_id,
  size_t msg_size,
  size_t payload_size,
  std::atomic<unsigned long long> &received_count,
  perf::single::latency_stats_builder_t &latency_builder)
{
    if (message.topic () != k_topic || message.parts ().size () != 1
        || message.parts ()[0].size () != payload_size) {
        return true;
    }

    perf_single_metric::header_t header;
    if (!perf_single_metric::decode_payload_header (
          message.parts ()[0].data (), message.parts ()[0].size (), &header)) {
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

void run_pattern_pubsub (const std::string &transport,
                         size_t msg_size,
                         const std::string &lib_name)
{
    if (!perf::single::transport_available (transport)) {
        if (perf_debug_enabled ())
            std::cerr << "pubsub: transport unavailable" << std::endl;
        std::cout << "UNSUPPORTED,PUBSUB," << transport << std::endl;
        return;
    }

    perf::single::ctx_guard_t ctx;
    if (!ctx.valid ()) {
        if (perf_debug_enabled ())
            std::cerr << "pubsub: invalid context" << std::endl;
        perf::single::print_fail_result (lib_name, "PUBSUB", transport, msg_size);
        return;
    }

    zlink::xpub_socket_t publisher (ctx.ctx ());
    zlink::sub_socket_t subscriber (ctx.ctx ());
    if (!publisher.valid () || !subscriber.valid ()) {
        if (perf_debug_enabled ())
            std::cerr << "pubsub: invalid sockets" << std::endl;
        perf::single::print_fail_result (lib_name, "PUBSUB", transport, msg_size);
        return;
    }

    zlink::socket_t publisher_socket = zlink::socket_t::wrap (publisher.handle ());
    zlink::socket_t subscriber_socket = zlink::socket_t::wrap (subscriber.handle ());
    (void) publisher_socket.set (zlink::pub_options::nodrop, 1);

    zlink::monitor_handle_t pub_monitor = zlink::monitor_handle_t::open (
      publisher, zlink::monitor_event::connection_ready_changed);
    zlink::monitor_handle_t sub_monitor = zlink::monitor_handle_t::open (
      subscriber, zlink::monitor_event::connection_ready_changed);
    if (!pub_monitor.valid () || !sub_monitor.valid ()) {
        if (perf_debug_enabled ())
            std::cerr << "pubsub: invalid monitors" << std::endl;
        perf::single::print_fail_result (lib_name, "PUBSUB", transport, msg_size);
        return;
    }

    if (!perf::single::setup_connected_pair (publisher_socket,
                                             subscriber_socket,
                                             transport,
                                             lib_name + "_pubsub")) {
        if (perf_debug_enabled ())
            std::cerr << "pubsub: setup_connected_pair failed errno=" << errno
                      << std::endl;
        perf::single::print_fail_result (lib_name, "PUBSUB", transport, msg_size);
        return;
    }

    const int recv_timeout = perf::single::resolve_single_pubsub_recv_timeout_ms ();
    (void) subscriber_socket.set_option (
      zlink::socket_options::rcvtimeo, recv_timeout);
    (void) subscriber.set_subscription (std::string (k_topic));

    std::this_thread::sleep_for (std::chrono::milliseconds (100));
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
                || !send_pubsub_payload (
                  &publisher_socket, payload.data (), payload.size ())) {
                sender_ok.store (false, std::memory_order_release);
                break;
            }
            sent_count.fetch_add (1, std::memory_order_release);
        }
        sender_done.store (true, std::memory_order_release);
    });

    zlink::poller_t poller;
    poller.add (subscriber_socket, zlink::poll_event::pollin);
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
            try {
                const zlink::topic_message_t message =
                  subscriber.subscribe (zlink::recv_flags_t::dontwait);
                if (!record_subscribed_payload (message,
                                               run_id,
                                               msg_size,
                                               payload_size,
                                               received_count,
                                               latency_builder)) {
                    sender_ok.store (false, std::memory_order_release);
                    break;
                }
            }
            catch (const zlink::recv_error_t &err) {
                if (err.result () == zlink::recv_result_t::no_data
                    || err.result () == zlink::recv_result_t::busy) {
                    break;
                }
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
        try {
            const zlink::topic_message_t message =
              subscriber.subscribe (zlink::recv_flags_t::dontwait);
            if (!record_subscribed_payload (message,
                                           run_id,
                                           msg_size,
                                           payload_size,
                                           received_count,
                                           latency_builder)) {
                sender_ok.store (false, std::memory_order_release);
                break;
            }
        }
        catch (const zlink::recv_error_t &err) {
            if (err.result () == zlink::recv_result_t::no_data
                || err.result () == zlink::recv_result_t::busy) {
                break;
            }
            sender_ok.store (false, std::memory_order_release);
            break;
        }
    }

    const unsigned long long received =
      received_count.load (std::memory_order_acquire);
    if (!sender_ok.load (std::memory_order_acquire) || received == 0
        || latency_builder.count () == 0) {
        if (perf_debug_enabled ())
            std::cerr << "pubsub: active phase failed received=" << received
                      << std::endl;
        perf::single::print_fail_result (
          lib_name, "PUBSUB", transport, msg_size);
        return;
    }
    const perf::single::latency_stats_t latency = latency_builder.snapshot ();

    const double throughput =
      static_cast<double> (received) / static_cast<double> (duration_s);
    perf::single::print_result (lib_name,
                                "PUBSUB",
                                transport,
                                msg_size,
                                throughput,
                                latency.mean_ns,
                                latency.p95_ns,
                                latency.p99_ns);
}

int main (int argc, char **argv)
{
    return perf::single::run_standard_bench_main (argc, argv, run_pattern_pubsub);
}
