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

int recv_router_router_header_flags (void *receiver_,
                                     size_t payload_size_,
                                     int flags_,
                                     perf_single_metric::header_t *header_out_,
                                     bool *header_ok_out_)
{
    if (!receiver_)
        return -1;

    if (header_ok_out_)
        *header_ok_out_ = false;

    zlink_routing_id_t source_rid;
    std::memset (&source_rid, 0, sizeof (source_rid));
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    const int rc = zlink_recv (
      receiver_, &source_rid, &parts, &part_count,
      static_cast<zlink_send_flags_t> (flags_));
    if (rc != 0) {
        const int err = zlink_errno ();
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }

    const bool rid_ok =
      source_rid.size == 7 && std::memcmp (source_rid.data, "ROUTER2", 7) == 0;
    const bool shape_ok = rid_ok && parts && part_count == 1;
    if (!shape_ok) {
        if (parts)
            zlink_multipart_close (parts, part_count);
        return -1;
    }

    const size_t actual_size = zlink_msg_size (&parts[0]);
    const bool size_ok = actual_size == payload_size_;
    bool header_ok = false;
    if (size_ok && header_out_) {
        header_ok = perf_single_metric::decode_payload_header (
          zlink_msg_data (&parts[0]), actual_size, header_out_);
    }
    zlink_multipart_close (parts, part_count);
    if (!size_ok)
        return -1;

    if (header_ok_out_)
        *header_ok_out_ = header_ok;
    return 1;
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

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (std::max (1, duration_s_));
    const auto drain_idle_limit =
      std::chrono::milliseconds (recv_timeout_ms_ > 0 ? recv_timeout_ms_ : 200);

    std::atomic<unsigned long long> sent_count (0);
    std::atomic<bool> sender_ok (true);
    std::atomic<bool> sender_done (false);
    std::atomic<unsigned long long> received (0);
    latency_stats_builder_t latency_builder;

    std::thread receiver_thread ([&]() {
        auto last_recv_at = std::chrono::steady_clock::now ();
        while (true) {
            const bool done = sender_done.load (std::memory_order_acquire);
            perf_single_metric::header_t header;
            bool header_ok = false;
            const int recv_rc = recv_router_router_header_flags (
              receiver_, state_->payload_size, 0, &header, &header_ok);
            if (recv_rc > 0) {
                last_recv_at = std::chrono::steady_clock::now ();
                if (header_ok && single_header_matches_run (*state_, header)
                    && std::chrono::steady_clock::now () < deadline) {
                    received.fetch_add (1, std::memory_order_relaxed);
                    latency_builder.add (single_latency_us (header));
                }

                for (;;) {
                    perf_single_metric::header_t burst_header;
                    bool burst_header_ok = false;
                    const int burst_rc = recv_router_router_header_flags (
                      receiver_,
                      state_->payload_size,
                      ZLINK_DONTWAIT,
                      &burst_header,
                      &burst_header_ok);
                    if (burst_rc > 0) {
                        last_recv_at = std::chrono::steady_clock::now ();
                        if (burst_header_ok
                            && single_header_matches_run (*state_, burst_header)
                            && std::chrono::steady_clock::now () < deadline) {
                            received.fetch_add (1, std::memory_order_relaxed);
                            latency_builder.add (
                              single_latency_us (burst_header));
                        }
                        continue;
                    }
                    if (burst_rc == 0)
                        break;
                    sender_ok.store (false, std::memory_order_release);
                    return;
                }
                continue;
            }

            if (recv_rc == 0) {
                if (done
                    && std::chrono::steady_clock::now () - last_recv_at
                         >= drain_idle_limit) {
                    break;
                }
                continue;
            }

            sender_ok.store (false, std::memory_order_release);
            return;
        }
    });

    std::thread sender_thread ([&]() {
        sender_ok.store (
          send_router_samples (
            sender_, payload_, state_, duration_s_, &sent_count),
          std::memory_order_release);
        sender_done.store (true, std::memory_order_release);
    });

    sender_thread.join ();
    receiver_thread.join ();
    if (!sender_ok.load (std::memory_order_acquire))
        return false;

    *received_out_ = received.load (std::memory_order_relaxed);
    if (*received_out_ == 0 || latency_builder.count () == 0)
        return false;
    *latency_out_ = latency_builder.snapshot ();
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
    state.run_id = next_single_metric_run_id ();
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
