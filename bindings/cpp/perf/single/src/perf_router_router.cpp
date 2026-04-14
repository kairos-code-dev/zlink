// ROUTER-ROUTER benchmark: one-way router->router with explicit handshake.

#include "../common/perf_single_common.hpp"
#include "../common/perf_single_runner.hpp"

#include <cstring>
#include <vector>

namespace {

const char *const k_receiver_id = "ROUTER1";
const char *const k_sender_id = "ROUTER2";

bool perf_debug_enabled ()
{
    return std::getenv ("PERF_DEBUG") != NULL;
}

struct router_router_recv_state_t
{
    router_router_recv_state_t ()
        : run_id (0),
          msg_size (0),
          payload_size (0),
          active_received (0),
          latency ()
    {
    }

    uint32_t run_id;
    size_t msg_size;
    size_t payload_size;
    std::atomic<unsigned long long> active_received;
    perf::single::latency_stats_builder_t latency;
};

bool complete_handshake (zlink::socket_t &receiver, zlink::socket_t &sender)
{
    zlink_routing_id_t receiver_rid = zlink::empty_routing_id ();
    zlink_routing_id_t sender_rid = zlink::empty_routing_id ();
    zlink::message_t outbound = zlink::message_t::from_string ("PING");

    if (zlink::routing_id_from (k_receiver_id, &receiver_rid) != 0
        || zlink::routing_id_from (k_sender_id, &sender_rid) != 0
        || !outbound.valid () || sender.send (receiver_rid, outbound) != 0) {
        if (perf_debug_enabled ())
            std::cerr << "router_router: handshake request failed errno="
                      << errno << std::endl;
        return false;
    }

    zlink::received_t inbound;
    if (receiver.receive (inbound, zlink::recv_flags_t::none) != 0) {
        if (perf_debug_enabled ())
            std::cerr << "router_router: handshake receive failed errno="
                      << errno << std::endl;
        return false;
    }
    if (!inbound.routing_id ().has_value () || inbound.parts ().size () != 1
        || inbound.parts ()[0].to_string () != "PING") {
        if (perf_debug_enabled ())
            std::cerr << "router_router: handshake receive invalid errno="
                      << errno << std::endl;
        return false;
    }

    zlink::message_t reply = zlink::message_t::from_string ("PONG");
    if (!reply.valid () || receiver.send (sender_rid, reply) != 0) {
        if (perf_debug_enabled ())
            std::cerr << "router_router: handshake reply send failed errno="
                      << errno << std::endl;
        return false;
    }

    zlink::received_t response;
    if (sender.receive (response, zlink::recv_flags_t::none) != 0) {
        if (perf_debug_enabled ())
            std::cerr << "router_router: handshake response recv failed errno="
                      << errno << std::endl;
        return false;
    }
    const bool ok = response.routing_id ().has_value ()
                    && response.parts ().size () == 1
                    && response.parts ()[0].to_string () == "PONG";
    if (!ok && perf_debug_enabled ())
        std::cerr << "router_router: handshake response failed errno=" << errno
                  << std::endl;
    return ok;
}

bool record_router_router_sample (uint32_t run_id_,
                                  size_t msg_size_,
                                  size_t payload_size_,
                                  zlink::message_t &part_,
                                  perf::single::latency_stats_builder_t *latency_,
                                  std::atomic<unsigned long long> *received_)
{
    if (!latency_ || !received_)
        return false;

    if (part_.size () != payload_size_)
        return true;

    perf_single_metric::header_t header;
    if (!perf_single_metric::decode_payload_header (
          part_.data (), part_.size (), &header)) {
        return true;
    }

    if (!perf_single_metric::is_expected (
          header, run_id_, perf_single_metric::phase_active, msg_size_)) {
        return true;
    }

    received_->fetch_add (1, std::memory_order_release);
    const uint64_t now = perf_single_metric::now_ns ();
    const double latency_ns =
      now >= header.sent_ts_ns ? static_cast<double> (now - header.sent_ts_ns)
                               : 0.0;
    latency_->add (latency_ns);
    return true;
}

bool send_router_samples (zlink::socket_t *sender_,
                          std::vector<char> *payload_,
                          router_router_recv_state_t *state_,
                          int duration_s_,
                          std::atomic<unsigned long long> *sent_count_)
{
    if (!sender_ || !payload_ || !state_ || !sent_count_)
        return false;

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (std::max (1, duration_s_));
    uint64_t seq = 1;
    while (std::chrono::steady_clock::now () < deadline) {
        if (!perf_single_metric::stamp_payload (
              payload_->data (),
              payload_->size (),
              state_->run_id,
              perf_single_metric::phase_active,
              state_->msg_size,
              seq,
              perf_single_metric::now_ns ())) {
            return false;
        }

        zlink_routing_id_t target = zlink::empty_routing_id ();
        if (zlink::routing_id_from (k_receiver_id, &target) != 0)
            return false;

        zlink::message_t msg =
          zlink::message_t::from_bytes (payload_->data (), payload_->size ());
        if (!msg.valid () || sender_->send (target, msg) != 0) {
            if (perf_debug_enabled ())
                std::cerr << "router_router: send failed errno=" << errno
                          << std::endl;
            return false;
        }

        sent_count_->fetch_add (1, std::memory_order_release);
        ++seq;
    }

    return true;
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

    const uint32_t run_id = static_cast<uint32_t> (perf_single_metric::now_ns ());

    const int duration_s =
      std::max (1, perf::single::resolve_single_duration_seconds ());
    std::atomic<unsigned long long> sent_count (0);
    std::atomic<bool> sender_ok (true);
    std::atomic<bool> sender_done (false);
    router_router_recv_state_t state;
    state.run_id = run_id;
    state.msg_size = msg_size;
    state.payload_size = payload_size;
    std::thread sender_thread ([&]() {
        sender_ok.store (
          send_router_samples (
            &sender.sock (), &payload, &state, duration_s, &sent_count),
          std::memory_order_release);
        sender_done.store (true, std::memory_order_release);
    });
    unsigned long long received = 0;
    perf::single::latency_stats_t latency;
    while (!sender_done.load (std::memory_order_acquire)) {
        try {
            zlink::received_t inbound;
            if (receiver.sock ().receive (inbound, zlink::recv_flags_t::none)
                != 0) {
                sender_ok.store (false, std::memory_order_release);
                break;
            }
            if (!record_router_router_sample (run_id,
                                              msg_size,
                                              payload_size,
                                              const_cast<zlink::message_t &> (
                                                inbound.parts ()[0]),
                                              &state.latency,
                                              &state.active_received)) {
                sender_ok.store (false, std::memory_order_release);
                break;
            }
        }
        catch (const zlink::recv_error_t &err) {
            if (err.result () == zlink::recv_result_t::no_data
                || err.result () == zlink::recv_result_t::busy) {
                continue;
            }
            if (perf_debug_enabled ())
                std::cerr << "router_router: recv failed result="
                          << static_cast<int> (err.result ())
                          << " errno=" << err.internal_errno () << std::endl;
            sender_ok.store (false, std::memory_order_release);
            break;
        }
    }

    sender_thread.join ();
    if (!sender_ok.load (std::memory_order_acquire)) {
        perf::single::print_fail_result (
          lib_name, "ROUTER_ROUTER", transport, msg_size);
        return;
    }

    const auto drain_deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (
        perf::single::resolve_single_recv_timeout_ms () * 2);
    while (state.active_received.load (std::memory_order_acquire)
             < sent_count.load (std::memory_order_acquire)
           && std::chrono::steady_clock::now () < drain_deadline) {
        try {
            zlink::received_t inbound;
            if (receiver.sock ().receive (inbound, zlink::recv_flags_t::dontwait)
                != 0) {
                if (errno == EAGAIN || errno == EINTR)
                    break;
                perf::single::print_fail_result (
                  lib_name, "ROUTER_ROUTER", transport, msg_size);
                return;
            }
            if (!record_router_router_sample (run_id,
                                              msg_size,
                                              payload_size,
                                              const_cast<zlink::message_t &> (
                                                inbound.parts ()[0]),
                                              &state.latency,
                                              &state.active_received)) {
                perf::single::print_fail_result (
                  lib_name, "ROUTER_ROUTER", transport, msg_size);
                return;
            }
        }
        catch (const zlink::recv_error_t &err) {
            perf::single::print_fail_result (
              lib_name, "ROUTER_ROUTER", transport, msg_size);
            return;
        }
    }

    received = state.active_received.load (std::memory_order_acquire);
    if (received == 0 || state.latency.count () == 0) {
        if (perf_debug_enabled ())
            std::cerr << "router_router: no active data sent="
                      << sent_count.load (std::memory_order_acquire)
                      << " received=" << received << std::endl;
        perf::single::print_fail_result (
          lib_name, "ROUTER_ROUTER", transport, msg_size);
        return;
    }
    latency = state.latency.snapshot ();

    const double throughput =
      static_cast<double> (received) / static_cast<double> (duration_s);
    perf::single::print_result (lib_name,
                                "ROUTER_ROUTER",
                                transport,
                                msg_size,
                                throughput,
                                latency.mean_ns,
                                latency.p95_ns,
                                latency.p99_ns);
}

int main (int argc, char **argv)
{
    return perf::single::run_standard_bench_main (argc, argv, run_pattern_router_router);
}
