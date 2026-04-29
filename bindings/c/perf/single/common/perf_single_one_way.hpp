#ifndef PERF_SINGLE_ONE_WAY_HPP
#define PERF_SINGLE_ONE_WAY_HPP

#include "bench_common.hpp"
#include "perf_single_latency.hpp"
#include "perf_single_metric_header.hpp"
#include "perf_single_phase.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

namespace perf_single_one_way {

struct state_t
{
    state_t () : run_id (0), msg_size (0), payload_size (0), active_received (0) {}

    uint32_t run_id;
    size_t msg_size;
    size_t payload_size;
    std::atomic<unsigned long long> active_received;
    latency_stats_builder_t latency;
};

enum send_step_t
{
    send_step_sent = 0,
    send_step_retry = 1,
    send_step_fatal = 2
};

inline int recv_single_part_header_flags (
  void *socket_,
  size_t expected_size_,
  int flags_,
  const char *trace_label_,
  perf_single_metric::header_t *header_out_,
  bool *header_ok_out_)
{
    if (!socket_)
        return -1;

    if (header_ok_out_)
        *header_ok_out_ = false;

    const zlink_routing_id_t *source_rid = NULL;
    zlink_msg_t part;
    zlink_part_flag_t has_more = ZLINK_PART_FINAL;
    if (zlink_msg_init (&part) != 0)
        return -1;
    const int rc = zlink_recv_part (
      socket_,
      &source_rid,
      &part,
      &has_more,
      static_cast<zlink_recv_flags_t> (flags_));
    if (rc != 0) {
        const int err = zlink_errno ();
        zlink_msg_close (&part);
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }

    if (source_rid || has_more != ZLINK_PART_FINAL) {
        if (bench_debug_enabled ()) {
            std::cerr << "[" << (trace_label_ ? trace_label_ : "perf-single")
                      << "] unexpected recv metadata has_more="
                      << static_cast<int> (has_more) << std::endl;
        }
        zlink_msg_close (&part);
        return -1;
    }

    const size_t actual_size = zlink_msg_size (&part);
    const bool size_ok = actual_size == expected_size_;
    bool header_ok = false;
    if (size_ok && header_out_) {
        header_ok = perf_single_metric::decode_payload_header (
          zlink_msg_data (&part), actual_size, header_out_);
    }
    zlink_msg_close (&part);
    if (!size_ok) {
        if (bench_debug_enabled ()) {
            std::cerr << "[" << (trace_label_ ? trace_label_ : "perf-single")
                      << "] unexpected payload size=" << actual_size
                      << " expected=" << expected_size_ << std::endl;
        }
        return -1;
    }

    if (header_ok_out_)
        *header_ok_out_ = header_ok;
    return 1;
}

template <typename StateT>
inline bool init_active_payload_part (zlink_msg_t *part_out_,
                                      std::vector<char> *payload_,
                                      const StateT &state_,
                                      uint64_t seq_)
{
    if (!part_out_ || !payload_)
        return false;

    if (!perf_single_metric::stamp_payload (
          payload_->data (),
          payload_->size (),
          state_.run_id,
          perf_single_metric::phase_active,
          state_.msg_size,
          seq_,
          perf_single_metric::now_ns ())) {
        return false;
    }

    if (zlink_msg_init_size (part_out_, payload_->size ()) != 0)
        return false;
    if (!payload_->empty ()) {
        std::memcpy (
          zlink_msg_data (part_out_), payload_->data (), payload_->size ());
    }
    return true;
}

inline send_step_t send_socket_active_message (void *sender_,
                                               zlink_msg_t *part_,
                                               int flags_,
                                               bool retry_on_eagain_)
{
    if (!sender_ || !part_)
        return send_step_fatal;

    if (zlink_send (
          sender_, part_, 1, static_cast<zlink_send_flags_t> (flags_))
        == 0) {
        return send_step_sent;
    }

    const int err = zlink_errno ();
    zlink_msg_close (part_);
    if (err == EINTR
        || (retry_on_eagain_
            && (err == EAGAIN || err == EWOULDBLOCK || err == ETIMEDOUT)))
        return send_step_retry;
    return send_step_fatal;
}

template <typename StateT, typename SendStepFn>
inline bool send_active_samples (void *sender_,
                                 std::vector<char> *payload_,
                                 const StateT &state_,
                                 int duration_s_,
                                 std::atomic<unsigned long long> *sent_count_,
                                 SendStepFn send_step_fn_)
{
    if (!sender_ || !payload_ || !sent_count_)
        return false;

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (std::max (1, duration_s_));
    uint64_t seq = 1;
    while (std::chrono::steady_clock::now () < deadline) {
        const send_step_t send_step =
          send_step_fn_ (sender_, payload_, state_, seq);
        if (send_step == send_step_fatal)
            return false;
        if (send_step == send_step_retry)
            continue;

        sent_count_->fetch_add (1, std::memory_order_release);
        ++seq;
    }

    return true;
}

template <typename StateT, typename SendStepFn, typename RecvHeaderFn>
inline bool run_active_phase (void *sender_,
                              void *receiver_,
                              std::vector<char> *payload_,
                              StateT *state_,
                              int duration_s_,
                              int recv_timeout_ms_,
                              const char *trace_label_,
                              SendStepFn send_step_fn_,
                              RecvHeaderFn recv_header_fn_,
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

    std::thread receiver_thread ([&] () {
        auto last_recv_at = std::chrono::steady_clock::now ();
        while (true) {
            const bool done = sender_done.load (std::memory_order_acquire);
            perf_single_metric::header_t header;
            bool header_ok = false;
            const int recv_rc = recv_header_fn_ (
              receiver_, state_->payload_size, 0, &header, &header_ok);
            if (recv_rc > 0) {
                last_recv_at = std::chrono::steady_clock::now ();
                if (header_ok && single_header_matches_run (*state_, header)
                    && std::chrono::steady_clock::now () < deadline) {
                    received.fetch_add (1, std::memory_order_relaxed);
                    latency_builder.add (single_latency_ns (header));
                }

                for (;;) {
                    perf_single_metric::header_t burst_header;
                    bool burst_header_ok = false;
                    const int burst_rc = recv_header_fn_ (
                      receiver_,
                      state_->payload_size,
                      ZLINK_DONTWAIT,
                      &burst_header,
                      &burst_header_ok);
                    if (burst_rc > 0) {
                        last_recv_at = std::chrono::steady_clock::now ();
                        if (burst_header_ok
                            && single_header_matches_run (
                              *state_, burst_header)
                            && std::chrono::steady_clock::now () < deadline) {
                            received.fetch_add (1, std::memory_order_relaxed);
                            latency_builder.add (
                              single_latency_ns (burst_header));
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

    std::thread sender_thread ([&] () {
        sender_ok.store (
          send_active_samples (
            sender_, payload_, *state_, duration_s_, &sent_count, send_step_fn_),
          std::memory_order_release);
        sender_done.store (true, std::memory_order_release);
    });

    sender_thread.join ();
    receiver_thread.join ();
    if (!sender_ok.load (std::memory_order_acquire)) {
        if (bench_debug_enabled ()) {
            std::cerr << "[" << (trace_label_ ? trace_label_ : "perf-single")
                      << "] active phase failed sent="
                      << sent_count.load (std::memory_order_relaxed)
                      << " received="
                      << received.load (std::memory_order_relaxed)
                      << std::endl;
        }
        return false;
    }

    *received_out_ = received.load (std::memory_order_relaxed);
    if (*received_out_ == 0)
        return false;
    *latency_out_ = latency_builder.snapshot ();
    return latency_builder.count () > 0;
}

} // namespace perf_single_one_way

#endif
