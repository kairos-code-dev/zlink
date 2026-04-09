// DEALER-ROUTER benchmark: one-way dealer->router.

#include "../common/perf_single_common.hpp"
#include "../common/perf_single_runner.hpp"

#include <vector>

namespace {

bool send_single_part (void *userdata_, const void *data_, size_t size_)
{
    zlink::socket_t *socket = static_cast<zlink::socket_t *> (userdata_);
    if (!socket)
        return false;

    zlink::message_t msg = zlink::message_t::from_bytes (data_, size_);
    return msg.valid () && socket->send (msg, zlink::send_flag::none) == 0;
}

} // namespace

void run_pattern_dealer_router (const std::string &transport,
                                size_t msg_size,
                                const std::string &lib_name)
{
    if (!perf::single::transport_available (transport)) {
        std::cout << "UNSUPPORTED,DEALER_ROUTER," << transport << std::endl;
        return;
    }

    perf::single::ctx_guard_t ctx;
    if (!ctx.valid ()) {
        perf::single::print_fail_result (
          lib_name, "DEALER_ROUTER", transport, msg_size);
        return;
    }

    perf::single::socket_guard_t router (ctx, zlink::socket_type::router);
    perf::single::socket_guard_t dealer (ctx, zlink::socket_type::dealer);
    if (!router.valid () || !dealer.valid ()) {
        perf::single::print_fail_result (
          lib_name, "DEALER_ROUTER", transport, msg_size);
        return;
    }

    (void) dealer.sock ().set_routing_id (std::string ("CLIENT"));

    if (!perf::single::setup_connected_pair (router.sock (),
                                             dealer.sock (),
                                             transport,
                                             lib_name + "_dealer_router")) {
        perf::single::print_fail_result (
          lib_name, "DEALER_ROUTER", transport, msg_size);
        return;
    }

    const int recv_timeout = perf::single::resolve_single_recv_timeout_ms ();
    (void) router.sock ().set_option (zlink::socket_options::rcvtimeo, recv_timeout);
    (void) dealer.sock ().set_option (
      zlink::socket_options::sndtimeo, perf::single::resolve_single_send_timeout_ms ());

    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 'a');
    perf::single::queue_probe_t queue_probe (&dealer.sock (), &router.sock ());
    perf::single::callback_receiver_t receiver_cb;
    if (!receiver_cb.attach (router.sock (), &queue_probe)) {
        perf::single::print_fail_result (
          lib_name, "DEALER_ROUTER", transport, msg_size, &queue_probe);
        return;
    }

    const uint32_t run_id = static_cast<uint32_t> (perf_single_metric::now_us ());
    uint64_t seq = 1;

    const int duration_s =
      std::max (1, perf::single::resolve_single_duration_seconds ());
    unsigned long long received = 0;
    perf::single::latency_stats_t latency;
    if (!perf::single::run_callback_phase (receiver_cb,
                                           &send_single_part,
                                           &dealer.sock (),
                                           payload,
                                           msg_size,
                                           run_id,
                                           seq,
                                           perf_single_metric::phase_active,
                                           0,
                                           duration_s,
                                           recv_timeout,
                                           &received,
                                           &latency)) {
        perf::single::print_fail_result (
          lib_name, "DEALER_ROUTER", transport, msg_size, &queue_probe);
        return;
    }

    const double throughput =
      static_cast<double> (received) / static_cast<double> (duration_s);
    perf::single::print_result (lib_name,
                                "DEALER_ROUTER",
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
    return perf::single::run_standard_bench_main (
      argc, argv, run_pattern_dealer_router);
}
