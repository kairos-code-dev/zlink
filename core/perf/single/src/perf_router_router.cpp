#include "../common/bench_common.hpp"
#include "../common/perf_single_latency.hpp"
#include "../common/perf_single_metric_header.hpp"
#include "../common/perf_single_monitor.hpp"
#include "../common/perf_single_phase.hpp"

#include <zlink.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

namespace {

struct router_router_recv_state_t
{
    router_router_recv_state_t () :
        run_id (0),
        msg_size (0),
        payload_size (0),
        active_received (0)
    {
    }

    uint32_t run_id;
    size_t msg_size;
    size_t payload_size;
    std::atomic<unsigned long long> active_received;
    latency_stats_builder_t latency;
};

bool setup_router_router_session (void *receiver_,
                                  void *sender_,
                                  const std::string &transport_,
                                  const std::string &pair_id_)
{
    if (!receiver_ || !sender_)
        return false;

    zlink_set_routing_id (receiver_, "ROUTER1", 7);
    zlink_set_routing_id (sender_, "ROUTER2", 7);
    zlink_set_router_option (sender_, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID,
                             "ROUTER1", 7);

    if (!setup_tls_server (receiver_, transport_)
        || !setup_tls_client (sender_, transport_)) {
        return false;
    }

    apply_single_hwm (receiver_);
    apply_single_hwm (sender_);

    const std::string endpoint =
      bind_and_resolve_endpoint (receiver_, transport_, pair_id_);
    if (endpoint.empty ())
        return false;

    void *receiver_monitor =
      open_configured_socket_monitor (receiver_, ZLINK_EVENT_CONNECTION_READY);
    if (!receiver_monitor)
        return false;
    void *sender_monitor =
      open_configured_socket_monitor (sender_, ZLINK_EVENT_CONNECTION_READY);
    if (!sender_monitor) {
        zlink_monitor_close (&receiver_monitor);
        return false;
    }

    if (!connect_checked (sender_, endpoint)) {
        zlink_monitor_close (&sender_monitor);
        zlink_monitor_close (&receiver_monitor);
        return false;
    }

    apply_single_benchmark_socket_options (receiver_, transport_);
    apply_single_benchmark_socket_options (sender_, transport_);

    const int timeout_ms =
      parse_positive_env ("PERF_CONNECT_READY_TIMEOUT_MS", 3000);
    const bool receiver_ready = wait_for_socket_monitor_event (
      receiver_monitor, ZLINK_EVENT_CONNECTION_READY, timeout_ms);
    const bool sender_ready = wait_for_socket_monitor_event (
      sender_monitor, ZLINK_EVENT_CONNECTION_READY, timeout_ms);
    zlink_monitor_close (&sender_monitor);
    zlink_monitor_close (&receiver_monitor);
    return receiver_ready && sender_ready;
}

bool drain_router_router_socket (void *receiver_,
                                 router_router_recv_state_t *state_,
                                 bool *progressed_out_)
{
    if (progressed_out_)
        *progressed_out_ = false;
    if (!receiver_ || !state_)
        return false;

    bool progressed = false;
    while (true) {
        zlink_routing_id_t source_rid;
        std::memset (&source_rid, 0, sizeof (source_rid));
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        const int rc = zlink_recv (
          receiver_, &source_rid, &parts, &part_count, ZLINK_DONTWAIT);
        if (rc != 0) {
            const int err = zlink_errno ();
            if (err == EAGAIN || err == EINTR)
                break;
            return false;
        }

        progressed = true;
        perf_single_metric::header_t header;
        const bool rid_ok =
          source_rid.size == 7
          && std::memcmp (source_rid.data, "ROUTER2", 7) == 0;
        const bool header_ok =
          rid_ok && parts && part_count == 1
          && zlink_msg_size (&parts[0]) == state_->payload_size
          && perf_single_metric::decode_payload_header (
            zlink_msg_data (&parts[0]), zlink_msg_size (&parts[0]), &header);
        if (parts)
            zlink_multipart_close (parts, part_count);

        if (header_ok)
            (void) single_record_active_header (state_, header);
    }

    if (progressed_out_)
        *progressed_out_ = progressed;
    return true;
}

bool send_router_samples (void *sender_,
                          std::vector<char> *payload_,
                          router_router_recv_state_t *state_,
                          int duration_s_,
                          std::atomic<unsigned long long> *sent_count_)
{
    if (!sender_ || !payload_ || !state_ || !sent_count_)
        return false;

    zlink_routing_id_t target_rid;
    std::memset (&target_rid, 0, sizeof (target_rid));
    target_rid.size = 7;
    std::memcpy (target_rid.data, "ROUTER1", target_rid.size);

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
              perf_single_metric::now_us ())) {
            return false;
        }

        zlink_msg_t part;
        if (zlink_msg_init_size (&part, payload_->size ()) != 0)
            return false;
        if (!payload_->empty ())
            std::memcpy (
              zlink_msg_data (&part), payload_->data (), payload_->size ());

        if (zlink_send_rid (sender_, &target_rid, &part, 1, 0) != 0) {
            const int err = zlink_errno ();
            if (err == EINTR)
                continue;
            return false;
        }

        sent_count_->fetch_add (1, std::memory_order_release);
        ++seq;
    }

    return true;
}

bool run_active_phase (void *sender_,
                       void *receiver_,
                       std::vector<char> *payload_,
                       router_router_recv_state_t *state_,
                       int duration_s_,
                       int recv_timeout_ms_,
                       unsigned long long *received_out_,
                       latency_stats_t *latency_out_)
{
    if (!sender_ || !receiver_ || !payload_ || !state_ || !received_out_
        || !latency_out_) {
        return false;
    }

    state_->active_received.store (0, std::memory_order_release);
    state_->latency = latency_stats_builder_t ();

    std::atomic<unsigned long long> sent_count (0);
    std::atomic<bool> sender_ok (true);
    std::atomic<bool> sender_done (false);
    std::thread sender_thread ([&]() {
        sender_ok.store (
          send_router_samples (
            sender_, payload_, state_, duration_s_, &sent_count),
          std::memory_order_release);
        sender_done.store (true, std::memory_order_release);
    });

    while (!sender_done.load (std::memory_order_acquire)) {
        bool progressed = false;
        if (!drain_router_router_socket (receiver_, state_, &progressed)) {
            sender_ok.store (false, std::memory_order_release);
            break;
        }
        if (!progressed) {
            (void) wait_socket_event (
              receiver_, ZLINK_POLLIN, std::max (1, recv_timeout_ms_));
        }
    }

    sender_thread.join ();
    if (!sender_ok.load (std::memory_order_acquire))
        return false;

    const auto drain_deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (
        single_phase_completion_timeout_ms (duration_s_, recv_timeout_ms_));
    while (state_->active_received.load (std::memory_order_acquire)
             < sent_count.load (std::memory_order_acquire)
           && std::chrono::steady_clock::now () < drain_deadline) {
        bool progressed = false;
        if (!drain_router_router_socket (receiver_, state_, &progressed))
            return false;
        if (!progressed
            && !wait_socket_event_until (
              receiver_, ZLINK_POLLIN, drain_deadline)) {
            break;
        }
    }

    *received_out_ = state_->active_received.load (std::memory_order_acquire);
    if (*received_out_ == 0 || state_->latency.count () == 0)
        return false;
    *latency_out_ = state_->latency.snapshot ();
    return true;
}

} // namespace

void run_router_router (const std::string &transport,
                        size_t msg_size,
                        const std::string &lib_name)
{
    if (!transport_available (transport))
        return;

    auto print_fail = [&] () {
        print_fail_result (lib_name, "ROUTER_ROUTER", transport, msg_size);
    };

    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 'a');
    router_router_recv_state_t state;
    state.run_id = static_cast<uint32_t> (perf_single_metric::now_us ());
    state.msg_size = msg_size;
    state.payload_size = payload_size;

    ctx_guard_t ctx;
    if (!ctx.valid ()) {
        print_fail ();
        return;
    }

    socket_guard_t receiver (ctx.get (), ZLINK_SOCKET_ROUTER);
    socket_guard_t sender (ctx.get (), ZLINK_SOCKET_ROUTER);
    if (!receiver.valid () || !sender.valid ()) {
        print_fail ();
        return;
    }

    if (!setup_router_router_session (
          receiver.get (), sender.get (), transport,
          lib_name + "_router_router")) {
        print_fail ();
        return;
    }

    const int duration_s = std::max (1, resolve_single_duration_seconds ());
    const int recv_timeout_ms = resolve_single_recv_timeout_ms ();
    unsigned long long received = 0;
    latency_stats_t latency;
    if (!run_active_phase (sender.get (),
                           receiver.get (),
                           &payload,
                           &state,
                           duration_s,
                           recv_timeout_ms,
                           &received,
                           &latency)) {
        print_fail ();
        return;
    }

    print_result (lib_name,
                  "ROUTER_ROUTER",
                  transport,
                  msg_size,
                  static_cast<double> (received)
                    / static_cast<double> (duration_s),
                  latency.mean_us,
                  latency.p95_us,
                  latency.p99_us);
}

int main (int argc, char **argv)
{
    return run_standard_bench_main (
      argc, argv, "ROUTER_ROUTER", run_router_router);
}
