// ROUTER-ROUTER benchmark: one-way router->router with explicit handshake.

#include "../common/perf_single_common.hpp"
#include "../common/perf_single_runner.hpp"

#include <cstring>
#include <vector>

namespace {

const char *const k_receiver_id = "ROUTER1";
const char *const k_sender_id = "ROUTER2";

bool complete_handshake (zlink::socket_t &receiver, zlink::socket_t &sender)
{
    zlink::received_t inbound;
    zlink::message_t outbound = zlink::message_t::from_string ("PING");
    zlink_routing_id_t receiver_rid = zlink::empty_routing_id ();
    zlink_routing_id_t sender_rid = zlink::empty_routing_id ();

    if (zlink::routing_id_from (k_receiver_id, &receiver_rid) != 0
        || zlink::routing_id_from (k_sender_id, &sender_rid) != 0
        || !outbound.valid () || sender.send (receiver_rid, outbound) != 0
        || receiver.receive (inbound) != 0 || inbound.parts.size () != 1
        || inbound.parts[0].to_string () != "PING") {
        return false;
    }

    zlink::message_t reply = zlink::message_t::from_string ("PONG");
    if (!reply.valid () || receiver.send (sender_rid, reply) != 0)
        return false;

    inbound.parts.clear ();
    return sender.receive (inbound) == 0 && inbound.parts.size () == 1
           && inbound.parts[0].to_string () == "PONG";
}

bool send_routed_single_part (void *userdata_, const void *data_, size_t size_)
{
    zlink::socket_t *socket = static_cast<zlink::socket_t *> (userdata_);
    if (!socket)
        return false;

    zlink_routing_id_t target = zlink::empty_routing_id ();
    if (zlink::routing_id_from (k_receiver_id, &target) != 0)
        return false;

    zlink::message_t msg = zlink::message_t::from_bytes (data_, size_);
    return msg.valid () && socket->send (target, msg, zlink::send_flag::none) == 0;
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

    (void) receiver.sock ().set_routing_id (std::string (k_receiver_id));
    (void) sender.sock ().set_routing_id (std::string (k_sender_id));
    (void) receiver.sock ().set (zlink::router_options::mandatory, 1);
    (void) sender.sock ().set (zlink::router_options::mandatory, 1);

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
    (void) receiver.sock ().set_option (zlink::socket_options::rcvtimeo, recv_timeout);
    (void) sender.sock ().set_option (
      zlink::socket_options::sndtimeo, perf::single::resolve_single_send_timeout_ms ());

    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 'a');

    perf::single::queue_probe_t queue_probe (&sender.sock (), &receiver.sock ());
    perf::single::callback_receiver_t receiver_cb;
    if (!receiver_cb.attach (receiver.sock (), &queue_probe)) {
        perf::single::print_fail_result (
          lib_name, "ROUTER_ROUTER", transport, msg_size, &queue_probe);
        return;
    }

    const uint32_t run_id = static_cast<uint32_t> (perf_single_metric::now_us ());
    uint64_t seq = 1;

    const int warmup_count =
      perf::single::resolve_bench_count ("PERF_WARMUP_COUNT", 1000);
    unsigned long long warmup_received = 0;
    if (!perf::single::run_callback_phase (receiver_cb,
                                           &send_routed_single_part,
                                           &sender.sock (),
                                           payload,
                                           msg_size,
                                           run_id,
                                           seq,
                                           perf_single_metric::phase_warmup,
                                           warmup_count,
                                           0,
                                           recv_timeout,
                                           &warmup_received,
                                           NULL)) {
        perf::single::print_fail_result (
          lib_name, "ROUTER_ROUTER", transport, msg_size, &queue_probe);
        return;
    }

    const int duration_s =
      std::max (1, perf::single::resolve_single_duration_seconds ());
    unsigned long long received = 0;
    perf::single::latency_stats_t latency;
    if (!perf::single::run_callback_phase (receiver_cb,
                                           &send_routed_single_part,
                                           &sender.sock (),
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
                                perf::single::sample_queue_stats (&queue_probe));
}

int main (int argc, char **argv)
{
    return perf::single::run_standard_bench_main (argc, argv, run_pattern_router_router);
}
