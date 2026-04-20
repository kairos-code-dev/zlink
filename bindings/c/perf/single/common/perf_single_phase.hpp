#ifndef PERF_SINGLE_PHASE_HPP
#define PERF_SINGLE_PHASE_HPP

#include "perf_single_metric_header.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <iostream>
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

inline int single_phase_completion_timeout_ms (int duration_s_,
                                               int recv_timeout_ms_)
{
    const int duration_ms = std::max (1, duration_s_) * 5000;
    return std::max (5000, std::max (duration_ms, recv_timeout_ms_ * 10));
}

inline double single_latency_ns (const perf_single_metric::header_t &header_,
                                 double factor_ = 1.0)
{
    const uint64_t now_ns = perf_single_metric::now_ns ();
    if (header_.sent_ts_ns <= 0
        || now_ns < static_cast<uint64_t> (header_.sent_ts_ns)) {
        return 0.0;
    }
    return static_cast<double> (
             now_ns - static_cast<uint64_t> (header_.sent_ts_ns))
           * factor_;
}

template <typename StateT>
inline bool single_header_matches_run (
  const StateT &state_, const perf_single_metric::header_t &header_)
{
    return perf_single_metric::is_expected (
      header_,
      state_.run_id,
      perf_single_metric::phase_active,
      state_.msg_size);
}

template <typename StateT>
inline bool single_record_active_header (
  StateT *state_,
  const perf_single_metric::header_t &header_,
  double latency_factor_ = 1.0)
{
    if (!state_ || !single_header_matches_run (*state_, header_))
        return false;

    single_increment_counter (state_->active_received);
    state_->latency.add (single_latency_ns (header_, latency_factor_));
    return true;
}

inline bool single_perf_validate_recv_mode_for_pattern (const char *pattern)
{
    if (!pattern || !*pattern)
        return false;

    const char *mode = std::getenv ("PERF_RECV_MODE");
    if (!mode || !*mode)
        return true;

    std::string normalized (mode);
    std::transform (
      normalized.begin (), normalized.end (), normalized.begin (), ::tolower);
    if (normalized == "recv")
        return true;

    std::cerr << "policy violation: single perf supports recv only"
              << " pattern=" << pattern << std::endl;
    return false;
}

#endif
