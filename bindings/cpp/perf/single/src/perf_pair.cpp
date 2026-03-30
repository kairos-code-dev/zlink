// PAIR benchmark: one-way sender->receiver loop inside one process.
// Topology: sender(PAIR connect) -> receiver(PAIR bind)

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

void run_pattern_pair (const std::string &transport,
                       size_t msg_size,
                       const std::string &lib_name)
{
    if (!perf::single::transport_available (transport)) {
        std::cout << "UNSUPPORTED,PAIR," << transport << std::endl;
        return;
    }

    perf::single::ctx_guard_t ctx;
    if (!ctx.valid ()) {
        perf::single::print_fail_result (lib_name, "PAIR", transport, msg_size);
        return;
    }

    perf::single::socket_guard_t bind_socket (ctx, zlink::socket_type::pair);
    perf::single::socket_guard_t conn_socket (ctx, zlink::socket_type::pair);
    if (!bind_socket.valid () || !conn_socket.valid ()) {
        perf::single::print_fail_result (lib_name, "PAIR", transport, msg_size);
        return;
    }

    (void) bind_socket.sock ().set_option (zlink::socket_options::tcp_nodelay, 1);
    (void) conn_socket.sock ().set_option (zlink::socket_options::tcp_nodelay, 1);

    if (!perf::single::setup_connected_pair (bind_socket.sock (),
                                             conn_socket.sock (),
                                             transport,
                                             lib_name + "_pair")) {
        perf::single::print_fail_result (lib_name, "PAIR", transport, msg_size);
        return;
    }

    const int recv_timeout = perf::single::resolve_single_recv_timeout_ms ();
    (void) bind_socket.sock ().set_option (
      zlink::socket_options::rcvtimeo, recv_timeout);
    (void) conn_socket.sock ().set_option (
      zlink::socket_options::sndtimeo, perf::single::resolve_single_send_timeout_ms ());

    const int warmup_count =
      perf::single::resolve_bench_count ("PERF_SINGLE_WARMUP_COUNT", 1000);
    const int duration_s = perf::single::resolve_single_duration_seconds ();
    std::vector<char> payload (
      std::max<size_t> (msg_size, perf_single_metric::header_size ()), '\0');
    const size_t payload_size = payload.size ();

    perf::single::queue_probe_t queue_probe (&conn_socket.sock (), &bind_socket.sock ());
    perf::single::callback_receiver_t receiver_cb;
    if (!receiver_cb.attach (bind_socket.sock (), &queue_probe)) {
        perf::single::print_fail_result (
          lib_name, "PAIR", transport, msg_size, &queue_probe);
        return;
    }

    uint64_t seq = 0;
    unsigned long long warmup_received = 0;
    if (!perf::single::run_callback_phase (receiver_cb,
                                           &send_single_part,
                                           &conn_socket.sock (),
                                           payload,
                                           payload_size,
                                           1,
                                           seq,
                                           perf_single_metric::phase_warmup,
                                           warmup_count,
                                           duration_s,
                                           recv_timeout,
                                           &warmup_received,
                                           NULL)) {
        perf::single::print_fail_result (
          lib_name, "PAIR", transport, msg_size, &queue_probe);
        return;
    }

    perf::single::latency_stats_t latency;
    unsigned long long active_received = 0;
    if (!perf::single::run_callback_phase (receiver_cb,
                                           &send_single_part,
                                           &conn_socket.sock (),
                                           payload,
                                           payload_size,
                                           1,
                                           seq,
                                           perf_single_metric::phase_active,
                                           warmup_count,
                                           duration_s,
                                           recv_timeout,
                                           &active_received,
                                           &latency)) {
        perf::single::print_fail_result (
          lib_name, "PAIR", transport, msg_size, &queue_probe);
        return;
    }

    const double throughput =
      duration_s > 0 ? static_cast<double> (active_received) / duration_s : 0.0;
    const perf::single::queue_stats_t queue_stats =
      perf::single::sample_queue_stats (&queue_probe);
    perf::single::print_result (lib_name, "PAIR", transport, msg_size,
                                throughput, latency.mean_us,
                                latency.p95_us, latency.p99_us, queue_stats);
}

int main (int argc, char **argv)
{
    return perf::single::run_standard_bench_main (argc, argv, run_pattern_pair);
}
