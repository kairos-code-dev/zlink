// PUBSUB benchmark: one-way publisher->subscriber loop.
// Topology: publisher(PUB bind) -> subscriber(SUB connect)

#include "../common/perf_single_common.hpp"
#include "../common/perf_single_runner.hpp"

#include <atomic>
#include <chrono>
#include <optional>
#include <thread>
#include <vector>

namespace {

const std::string k_topic ("bench");

bool perf_debug_enabled ()
{
    return std::getenv ("PERF_DEBUG") != NULL;
}

bool send_pubsub_payload (void *userdata_, const void *data_, size_t size_)
{
    zlink::pub_socket_t *publisher = static_cast<zlink::pub_socket_t *> (userdata_);
    if (!publisher)
        return false;

    zlink::message_t part = zlink::message_t::from_bytes (data_, size_);
    if (!part.valid ())
        return false;
    try {
        if (!std::move (publisher->publish (k_topic))
               .message (part)
               .flags (zlink::send_flags_t::dontwait)
               .submit ()) {
            errno = EAGAIN;
            return false;
        }
        return true;
    }
    catch (const zlink::zlink_error_t &) {
        return false;
    }
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

    received_count.fetch_add (1, std::memory_order_relaxed);
    const uint64_t now = perf_single_metric::now_ns ();
    latency_builder.add (
      now >= header.sent_ts_ns ? static_cast<double> (now - header.sent_ts_ns)
                               : 0.0);
    return true;
}

} // namespace

bool run_pattern_pubsub (const std::string &transport,
                         size_t msg_size,
                         const std::string &lib_name)
{
    if (!perf::single::transport_available (transport)) {
        if (perf_debug_enabled ())
            std::cerr << "pubsub: transport unavailable" << std::endl;
        std::cout << "UNSUPPORTED," << lib_name << ",PUBSUB," << transport
                  << std::endl;
        return true;
    }

    perf::single::ctx_guard_t ctx;
    if (!ctx.valid ()) {
        if (perf_debug_enabled ())
            std::cerr << "pubsub: invalid context" << std::endl;
        return false;
    }

    zlink::pub_socket_t publisher (ctx.ctx ());
    zlink::sub_socket_t subscriber (ctx.ctx ());
    if (!publisher.valid () || !subscriber.valid ()) {
        if (perf_debug_enabled ())
            std::cerr << "pubsub: invalid sockets" << std::endl;
        return false;
    }

    publisher.options ().no_drop (true);
    (void) subscriber.set_subscription (std::string ());
    if (!perf::single::apply_single_auto_hwm_msg_unit (publisher, msg_size)
        || !perf::single::apply_single_auto_hwm_msg_unit (subscriber, msg_size)
        || !perf::single::recalculate_single_auto_hwm (ctx)) {
        if (perf_debug_enabled ())
            std::cerr << "pubsub: auto-hwm msg unit setup failed errno=" << errno
                      << std::endl;
        return false;
    }

    if (!perf::single::setup_connected_pair (publisher,
                                             subscriber,
                                             transport,
                                             lib_name + "_pubsub")) {
        if (perf_debug_enabled ())
            std::cerr << "pubsub: setup_connected_pair failed errno=" << errno
                      << std::endl;
        return false;
    }

    const int recv_timeout = perf::single::resolve_single_pubsub_recv_timeout_ms ();
    subscriber.options ().recv_timeout (std::chrono::milliseconds (recv_timeout));

    std::this_thread::sleep_for (std::chrono::milliseconds (
      perf::single::resolve_single_pubsub_ready_settle_ms ()));
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
                || !send_pubsub_payload (
                  &publisher, payload.data (), payload.size ())) {
                if (errno == EAGAIN || errno == EWOULDBLOCK
                    || errno == ETIMEDOUT || errno == EINTR) {
                    continue;
                }
                sender_ok.store (false, std::memory_order_release);
                break;
            }
            sent_count.fetch_add (1, std::memory_order_relaxed);
        }
        // PERF_SINGLE_TEST_POLICY § 1.4: publish wire-level stop token
        // under k_topic so the subscriber observes the terminator and
        // exits its blocking subscribe loop. Bounded retry through
        // transient backpressure.
        for (int retry = 0; retry < 100; ++retry) {
            if (send_pubsub_payload (&publisher,
                                     perf::single::k_stop_token,
                                     std::strlen (perf::single::k_stop_token))) {
                break;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK
                && errno != ETIMEDOUT && errno != EINTR) {
                break;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
    });

    auto record_if_active = [&] (const zlink::topic_message_t &message_) {
        if (std::chrono::steady_clock::now () >= active_deadline)
            return true;
        return record_subscribed_payload (message_,
                                          run_id,
                                          msg_size,
                                          payload_size,
                                          received_count,
                                          latency_builder);
    };

    auto is_stop_message = [] (const zlink::topic_message_t &message_) {
        return message_.parts ().size () == 1
               && perf::single::is_stop_token_message (message_.parts ()[0]);
    };

    bool stop_received = false;
    while (!stop_received) {
        // PERF_SINGLE_TEST_POLICY § 1.4: blocking subscribe wakes when
        // the stop token arrives on the wire.
        std::optional<zlink::topic_message_t> message;
        try {
            message = subscriber.subscribe ();
        }
        catch (const zlink::recv_error_t &error) {
            const int err = error.internal_errno ();
            if (err == EINTR || err == EAGAIN || err == EWOULDBLOCK
                || err == ETIMEDOUT)
                continue;
            sender_ok.store (false, std::memory_order_release);
            break;
        }
        if (!message.has_value ())
            continue;

        if (is_stop_message (*message)) {
            stop_received = true;
            break;
        }

        if (!record_if_active (*message)) {
            sender_ok.store (false, std::memory_order_release);
            break;
        }

        for (;;) {
            try {
                message = subscriber.subscribe (ZLINK_DONTWAIT);
            }
            catch (const zlink::recv_error_t &error) {
                const int err = error.internal_errno ();
                if (err == EINTR || err == EAGAIN || err == EWOULDBLOCK
                    || err == ETIMEDOUT)
                    break;
                sender_ok.store (false, std::memory_order_release);
                break;
            }
            if (!message.has_value ())
                break;

            if (is_stop_message (*message)) {
                stop_received = true;
                break;
            }

            if (!record_if_active (*message)) {
                sender_ok.store (false, std::memory_order_release);
                break;
            }
        }
    }

    sender_thread.join ();
    // Stop token is the last in-flight message, so any earlier payloads
    // have already been recorded above. No bounded drain loop needed.
    (void) recv_timeout;

    const unsigned long long received =
      received_count.load (std::memory_order_acquire);
    if (!sender_ok.load (std::memory_order_acquire) || received == 0
        || latency_builder.count () == 0) {
        if (perf_debug_enabled ())
            std::cerr << "pubsub: active phase failed received=" << received
                      << std::endl;
        return false;
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
    return true;
}

int main (int argc, char **argv)
{
    return perf::single::run_standard_bench_main (argc, argv, run_pattern_pubsub);
}
