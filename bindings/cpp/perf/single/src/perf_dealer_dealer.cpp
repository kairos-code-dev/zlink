// DEALER-DEALER benchmark: one-way sender->receiver loop inside one process.

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

    perf::single::queue_probe_t queue_probe (&conn_socket.sock (),
                                             &bind_socket.sock ());
    perf::single::callback_receiver_t receiver_cb;
    if (!receiver_cb.attach (bind_socket.sock (), &queue_probe)) {
        perf::single::print_fail_result (
          lib_name, "DEALER_DEALER", transport, msg_size, &queue_probe);
        return;
    }

    const uint32_t run_id = static_cast<uint32_t> (perf_single_metric::now_ns ());
    uint64_t seq = 1;

    const int duration_s =
      std::max (1, perf::single::resolve_single_duration_seconds ());
    unsigned long long received = 0;
    perf::single::latency_stats_t latency;
    if (!perf::single::run_callback_phase (receiver_cb,
                                           &send_single_part,
                                           &conn_socket.sock (),
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
                                latency.mean_ns,
                                latency.p95_ns,
                                latency.p99_ns,
                                perf::single::sample_queue_stats (&queue_probe));
}

int main (int argc, char **argv)
{
    return perf::single::run_standard_bench_main (
      argc, argv, run_pattern_dealer_dealer);
}
