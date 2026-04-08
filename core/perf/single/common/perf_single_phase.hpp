#ifndef PERF_SINGLE_PHASE_HPP
#define PERF_SINGLE_PHASE_HPP

#include "perf_single_metric_queue.hpp"
#include "perf_single_metric_header.hpp"

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>

enum perf_send_class_t
{
    perf_send_success = 0,
    perf_send_retry,
    perf_send_backpressure,
    perf_send_fatal
};

inline perf_send_class_t perf_classify_send_result (int rc_)
{
    if (rc_ >= 0)
        return perf_send_success;

    const int err = zlink_errno ();
    if (err == EINTR)
        return perf_send_retry;
    return err == EAGAIN ? perf_send_backpressure : perf_send_fatal;
}

inline void single_increment_counter (std::atomic<unsigned long long> &counter_)
{
    counter_.fetch_add (1, std::memory_order_relaxed);
}

inline void single_increment_counter (unsigned long long &counter_)
{
    ++counter_;
}

inline unsigned long long single_load_counter (
  const std::atomic<unsigned long long> &counter_)
{
    return counter_.load (std::memory_order_relaxed);
}

inline unsigned long long single_load_counter (
  const unsigned long long &counter_)
{
    return counter_;
}

inline uint64_t single_load_deadline (
  const std::atomic<uint64_t> &deadline_)
{
    return deadline_.load (std::memory_order_acquire);
}

inline uint64_t single_load_deadline (const uint64_t &deadline_)
{
    return deadline_;
}

inline bool single_load_flag (const std::atomic<bool> &flag_)
{
    return flag_.load (std::memory_order_acquire);
}

inline bool single_load_flag (const bool &flag_)
{
    return flag_;
}

inline void single_store_flag (std::atomic<bool> &flag_, bool value_)
{
    flag_.store (value_, std::memory_order_release);
}

inline void single_store_flag (bool &flag_, bool value_)
{
    flag_ = value_;
}

template <typename StateT>
inline void single_notify_metric_waiters (StateT *state_)
{
    if (!state_)
        return;
    state_->cv.notify_all ();
}

template <typename StateT>
inline auto single_set_phase_wait_armed_impl (StateT &state_,
                                              bool value_,
                                              int)
  -> decltype (state_.phase_wait_armed.store (value_,
                                              std::memory_order_release),
               void ())
{
    state_.phase_wait_armed.store (value_, std::memory_order_release);
}

template <typename StateT>
inline void single_set_phase_wait_armed_impl (StateT &, bool, long)
{
}

template <typename StateT>
inline void single_set_phase_wait_armed (StateT &state_, bool value_)
{
    single_set_phase_wait_armed_impl (state_, value_, 0);
}

template <typename StateT>
inline auto single_phase_wait_notify_armed_impl (const StateT &state_, int)
  -> decltype (state_.phase_wait_armed.load (std::memory_order_acquire), bool ())
{
    return state_.phase_wait_armed.load (std::memory_order_acquire);
}

template <typename StateT>
inline bool single_phase_wait_notify_armed_impl (const StateT &, long)
{
    return true;
}

template <typename StateT>
inline bool single_phase_wait_notify_armed (const StateT &state_)
{
    return single_phase_wait_notify_armed_impl (state_, 0);
}

template <typename StateT>
class single_phase_wait_arm_scope_t
{
  public:
    explicit single_phase_wait_arm_scope_t (StateT &state_) : _state (state_)
    {
        single_set_phase_wait_armed (_state, true);
    }

    ~single_phase_wait_arm_scope_t ()
    {
        single_set_phase_wait_armed (_state, false);
    }

  private:
    StateT &_state;
};

template <typename StateT>
inline void single_mark_callback_fatal (StateT *state_)
{
    if (!state_)
        return;
    single_store_flag (state_->fatal, true);
    single_notify_metric_waiters (state_);
}

template <typename StateT>
inline bool single_enqueue_metric_event (
  StateT *state_,
  const perf_single_metric::header_t &header_)
{
    if (!state_)
        return false;
    if (!state_->callback_queue)
        return false;

    single_callback_metric_event_t event;
    event.phase = header_.phase;
    event.sent_ts_us = header_.sent_ts_us;
    if (!state_->callback_queue->push (event)) {
        single_mark_callback_fatal (state_);
        return false;
    }
    state_->cv.notify_one ();
    return true;
}

template <typename StateT>
inline void single_note_callback_receive (
  StateT *state_, const perf_single_metric::header_t &header_)
{
    if (!state_)
        return;

    if (header_.phase
        == static_cast<uint32_t> (perf_single_metric::phase_active)) {
        single_increment_counter (state_->active_received);
    }

    if (single_phase_wait_notify_armed (*state_))
        state_->cv.notify_all ();
}

template <typename StateT>
inline unsigned long long single_load_phase_received (
  const StateT &state_, perf_single_metric::phase_t phase_)
{
    return phase_ == perf_single_metric::phase_active
             ? single_load_counter (state_.active_received)
             : 0;
}

template <typename StateT>
inline unsigned long long single_load_phase_processed (
  const StateT &state_, perf_single_metric::phase_t phase_)
{
    return phase_ == perf_single_metric::phase_active
             ? single_load_counter (state_.active_processed)
             : 0;
}

template <typename StateT>
inline bool single_wait_for_phase_processed (StateT &state_,
                                             perf_single_metric::phase_t phase_,
                                             unsigned long long expected_count_,
                                             int timeout_ms_)
{
    if (expected_count_ == 0)
        return !single_load_flag (state_.fatal);

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1);
    const auto idle_window =
      std::chrono::milliseconds (std::max (50, std::min (1000, timeout_ms_)));
    std::unique_lock<std::mutex> lock (state_.mutex);
    single_phase_wait_arm_scope_t<StateT> phase_wait_arm (state_);
    unsigned long long observed =
      single_load_phase_processed (state_, phase_);

    while (std::chrono::steady_clock::now () < deadline) {
        if (single_load_flag (state_.fatal))
            return false;
        if (observed >= expected_count_)
            return true;

        const auto idle_deadline = std::min (
          deadline, std::chrono::steady_clock::now () + idle_window);
        const bool advanced = state_.cv.wait_until (
          lock, idle_deadline, [&state_, phase_, observed, expected_count_]() {
                     return single_load_flag (state_.fatal)
                     || single_load_phase_processed (state_, phase_)
                          >= expected_count_
                     || single_load_phase_processed (state_, phase_) != observed;
          });
        if (single_load_flag (state_.fatal))
            return false;

        const unsigned long long current =
          single_load_phase_processed (state_, phase_);
        if (current >= expected_count_)
            return true;
        if (!advanced)
            return false;

        observed = current;
    }

    return !single_load_flag (state_.fatal)
           && single_load_phase_processed (state_, phase_) >= expected_count_;
}

inline int single_phase_completion_timeout_ms (int duration_s_,
                                               int recv_timeout_ms_)
{
    const int duration_ms = std::max (1, duration_s_) * 5000;
    return std::max (5000, std::max (duration_ms, recv_timeout_ms_ * 10));
}

inline bool single_perf_validate_recv_mode_for_pattern (const char *pattern)
{
    if (!pattern || !*pattern)
        return false;

    return true;
}

#endif
