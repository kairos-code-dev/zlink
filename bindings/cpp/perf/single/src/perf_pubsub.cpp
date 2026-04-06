// PUBSUB benchmark: one-way publisher->subscriber loop.
// Topology: publisher(PUB bind) -> subscriber(SUB connect)

#include "../common/perf_single_common.hpp"
#include "../common/perf_single_runner.hpp"

#include <vector>

namespace {

const char *const k_topic = "bench";

bool send_pubsub_payload (void *userdata_, const void *data_, size_t size_)
{
    zlink::socket_t *publisher = static_cast<zlink::socket_t *> (userdata_);
    if (!publisher)
        return false;

    zlink::message_t part = zlink::message_t::from_bytes (data_, size_);
    return part.valid () && publisher->publish (k_topic, part) == 0;
}

} // namespace

void run_pattern_pubsub (const std::string &transport,
                         size_t msg_size,
                         const std::string &lib_name)
{
    if (!perf::single::transport_available (transport)) {
        std::cout << "UNSUPPORTED,PUBSUB," << transport << std::endl;
        return;
    }

    perf::single::ctx_guard_t ctx;
    if (!ctx.valid ()) {
        perf::single::print_fail_result (lib_name, "PUBSUB", transport, msg_size);
        return;
    }

    zlink::xpub_socket_t publisher (ctx.ctx ());
    zlink::sub_socket_t subscriber (ctx.ctx ());
    if (!publisher.valid () || !subscriber.valid ()) {
        perf::single::print_fail_result (lib_name, "PUBSUB", transport, msg_size);
        return;
    }

    zlink::socket_t publisher_socket = zlink::socket_t::wrap (publisher.handle ());
    zlink::socket_t subscriber_socket = zlink::socket_t::wrap (subscriber.handle ());
    (void) publisher_socket.set (zlink::pub_options::nodrop, 1);

    zlink::monitor_handle_t pub_monitor (
      publisher, zlink::monitor_event::connection_ready_changed);
    zlink::monitor_handle_t sub_monitor (
      subscriber, zlink::monitor_event::connection_ready_changed);
    if (!pub_monitor.valid () || !sub_monitor.valid ()) {
        perf::single::print_fail_result (lib_name, "PUBSUB", transport, msg_size);
        return;
    }

    if (!perf::single::setup_connected_pair (publisher_socket,
                                             subscriber_socket,
                                             transport,
                                             lib_name + "_pubsub")) {
        perf::single::print_fail_result (lib_name, "PUBSUB", transport, msg_size);
        return;
    }

    const int recv_timeout = perf::single::resolve_single_pubsub_recv_timeout_ms ();
    (void) subscriber.set_option (zlink::socket_options::rcvtimeo, recv_timeout);
    (void) subscriber.set_subscription (std::string (k_topic));

    if (!perf::single::wait_socket_monitor_event (
          sub_monitor,
          static_cast<uint64_t> (zlink::monitor_event::connection_ready_changed),
          -1,
          10000)
        || !perf::single::wait_socket_monitor_event (
          pub_monitor,
          static_cast<uint64_t> (zlink::monitor_event::connection_ready_changed),
          -1,
          10000)) {
        perf::single::print_fail_result (
          lib_name, "PUBSUB", transport, msg_size);
        return;
    }
    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 'a');

    perf::single::queue_probe_t queue_probe (&publisher_socket, &subscriber_socket);
    perf::single::subscribe_callback_receiver_t receiver_cb;
    if (!receiver_cb.attach_socket (subscriber_socket, &queue_probe)) {
        perf::single::print_fail_result (
          lib_name, "PUBSUB", transport, msg_size, &queue_probe);
        return;
    }

    const uint32_t run_id = static_cast<uint32_t> (perf_single_metric::now_us ());
    uint64_t seq = 1;

    const int warmup_count =
      perf::single::resolve_bench_count ("PERF_WARMUP_COUNT", 1000);
    unsigned long long warmup_received = 0;
    if (!perf::single::run_subscribe_callback_phase (receiver_cb,
                                                     &send_pubsub_payload,
                                                     &publisher_socket,
                                                     payload,
                                                     msg_size,
                                                     run_id,
                                                     seq,
                                                     perf_single_metric::phase_warmup,
                                                     warmup_count,
                                                     0,
                                                     recv_timeout,
                                                     k_topic,
                                                     &warmup_received,
                                                     NULL)) {
        perf::single::print_fail_result (
          lib_name, "PUBSUB", transport, msg_size, &queue_probe);
        return;
    }

    const int duration_s =
      std::max (1, perf::single::resolve_single_duration_seconds ());
    unsigned long long received = 0;
    perf::single::latency_stats_t latency;
    if (!perf::single::run_subscribe_callback_phase (receiver_cb,
                                                     &send_pubsub_payload,
                                                     &publisher_socket,
                                                     payload,
                                                     msg_size,
                                                     run_id,
                                                     seq,
                                                     perf_single_metric::phase_active,
                                                     0,
                                                     duration_s,
                                                     recv_timeout,
                                                     k_topic,
                                                     &received,
                                                     &latency)) {
        perf::single::print_fail_result (
          lib_name, "PUBSUB", transport, msg_size, &queue_probe);
        return;
    }

    const double throughput =
      static_cast<double> (received) / static_cast<double> (duration_s);
    perf::single::print_result (lib_name,
                                "PUBSUB",
                                transport,
                                msg_size,
                                throughput,
                                latency.mean_us,
                                latency.p95_us,
                                latency.p99_us,
                                perf::single::sample_queue_stats (&queue_probe));
}

int main (int argc, char **argv)
{
    return perf::single::run_standard_bench_main (argc, argv, run_pattern_pubsub);
}
