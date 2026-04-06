#ifndef CPP_PERF_SPOT_CLIENT_RECV_HPP
#define CPP_PERF_SPOT_CLIENT_RECV_HPP

#include "perf_common.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace perf {
namespace multi {

template<typename OwnerT>
struct spot_recv_thread_metrics_t
{
    spot_recv_thread_metrics_t ()
        : owner (NULL),
          registered (false),
          epoch (0),
          active_received (0),
          sample_index (0),
          latency ()
    {
    }

    OwnerT *owner;
    bool registered;
    uint64_t epoch;
    unsigned long long active_received;
    unsigned long long sample_index;
    bench_latency_sampler_t latency;
};

template<typename SlotT>
struct spot_recv_worker_t
{
    zlink::poller_t poller;
    std::vector<SlotT *> slots;
    std::vector<zlink::poll_event_t> events;
    std::thread thread;
};

inline size_t resolve_spot_recv_worker_count (size_t slot_count_)
{
    return std::min<size_t> (
      slot_count_,
      static_cast<size_t> (std::max (
        1,
        parse_positive_env (
          "PERF_MULTI_SPOT_RECV_WORKERS",
          static_cast<int> (std::thread::hardware_concurrency ())))));
}

template<typename OwnerT>
inline spot_recv_thread_metrics_t<OwnerT> *bind_spot_recv_thread_metrics (
  OwnerT *owner_,
  std::mutex *metrics_mutex_,
  std::vector<spot_recv_thread_metrics_t<OwnerT> *> *thread_metrics_,
  std::atomic<uint64_t> *metrics_epoch_)
{
    if (!owner_ || !metrics_mutex_ || !thread_metrics_ || !metrics_epoch_)
        return NULL;

    static thread_local spot_recv_thread_metrics_t<OwnerT> metrics;
    if (!metrics.registered || metrics.owner != owner_) {
        metrics.owner = owner_;
        metrics.registered = true;
        std::lock_guard<std::mutex> lock (*metrics_mutex_);
        if (std::find (thread_metrics_->begin (),
                       thread_metrics_->end (),
                       &metrics)
            == thread_metrics_->end ()) {
            thread_metrics_->push_back (&metrics);
        }
    }

    const uint64_t epoch = metrics_epoch_->load (std::memory_order_acquire);
    if (metrics.epoch != epoch) {
        metrics.epoch = epoch;
        metrics.active_received = 0;
        metrics.sample_index = 0;
        metrics.latency = bench_latency_sampler_t ();
    }
    return &metrics;
}

template<typename OwnerT>
inline void collect_spot_recv_thread_metrics (
  OwnerT *owner_,
  std::mutex *metrics_mutex_,
  std::vector<spot_recv_thread_metrics_t<OwnerT> *> *thread_metrics_,
  std::atomic<uint64_t> *metrics_epoch_,
  unsigned long long *active_received_out_,
  bench_latency_stats_t *latency_out_)
{
    if (!owner_ || !metrics_mutex_ || !thread_metrics_ || !metrics_epoch_
        || !active_received_out_ || !latency_out_) {
        return;
    }

    bench_latency_sampler_t merged_latency;
    unsigned long long active_received = 0;
    const uint64_t epoch = metrics_epoch_->load (std::memory_order_acquire);
    {
        std::lock_guard<std::mutex> lock (*metrics_mutex_);
        for (size_t i = 0; i < thread_metrics_->size (); ++i) {
            spot_recv_thread_metrics_t<OwnerT> *metrics = (*thread_metrics_)[i];
            if (!metrics || metrics->owner != owner_ || metrics->epoch != epoch)
                continue;
            active_received += metrics->active_received;
            merged_latency.merge_from (metrics->latency);
        }
    }

    *active_received_out_ = active_received;
    *latency_out_ = merged_latency.snapshot ();
}

inline void print_spot_client_result_lines (
  const char *pattern_,
  const std::string &transport_,
  size_t msg_size_,
  unsigned long long active_count_,
  int duration_seconds_,
  const bench_latency_stats_t &latency_,
  const bench_multi_resource_metrics_t &resource_metrics_)
{
    const double throughput =
      static_cast<double> (active_count_)
      / static_cast<double> (std::max (1, duration_seconds_));
    const double bandwidth =
      throughput * static_cast<double> (msg_size_) / 1000000.0;

    print_result ("current",
                  pattern_,
                  transport_,
                  msg_size_,
                  throughput,
                  bandwidth,
                  latency_.mean_us,
                  latency_.p95_us,
                  latency_.p99_us);
    print_client_resource_metrics (
      "current", pattern_, transport_, msg_size_, resource_metrics_);
}

} // namespace multi
} // namespace perf

#endif
