#ifndef CPP_PERF_SPOT_THREAD_METRICS_HPP
#define CPP_PERF_SPOT_THREAD_METRICS_HPP

#include "../../common/perf_latency_sampler.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace perf {
namespace multi {

template<typename OwnerT>
struct spot_thread_metrics_t
{
    spot_thread_metrics_t ()
        : owner (NULL),
          registered (false),
          epoch (0),
          active_received (0),
          sample_index (0),
          latency (),
          latency_mutex ()
    {
    }

    OwnerT *owner;
    bool registered;
    uint64_t epoch;
    unsigned long long active_received;
    unsigned long long sample_index;
    latency_sampler_t latency;
    // Per-thread latency mutex: the recv worker holds it briefly around
    // each latency.add(); collect_spot_thread_metrics holds it briefly
    // when reading the same thread's samples. This prevents the hot
    // path from racing with collect_*'s merge_from() across the
    // _samples vector reallocation boundary. Cost on the hot path is
    // an uncontended lock per recv (~10ns).
    std::mutex latency_mutex;
};

template<typename OwnerT>
inline spot_thread_metrics_t<OwnerT> *bind_spot_thread_metrics (
  OwnerT *owner_,
  std::mutex *metrics_mutex_,
  std::vector<spot_thread_metrics_t<OwnerT> *> *thread_metrics_,
  std::atomic<uint64_t> *metrics_epoch_)
{
    if (!owner_ || !metrics_mutex_ || !thread_metrics_ || !metrics_epoch_)
        return NULL;

    static thread_local spot_thread_metrics_t<OwnerT> metrics;
    if (!metrics.registered || metrics.owner != owner_) {
        std::lock_guard<std::mutex> lock (*metrics_mutex_);
        metrics.owner = owner_;
        metrics.registered = true;
        if (std::find (thread_metrics_->begin (),
                       thread_metrics_->end (),
                       &metrics)
            == thread_metrics_->end ()) {
            thread_metrics_->push_back (&metrics);
        }
    }

    const uint64_t epoch = metrics_epoch_->load (std::memory_order_acquire);
    if (metrics.epoch != epoch) {
        // Reset under the metrics mutex + per-thread latency mutex so
        // collect_spot_thread_metrics (which scans the thread_metrics list
        // under the same shared mutex AND grabs the per-thread mutex while
        // reading) is not concurrently iterating the old latency vector
        // when this thread replaces it. Without these locks, a recv worker
        // that crosses an epoch boundary destructs the old _samples vector
        // while the main thread merge_from reads its iterators → SEGV.
        std::lock_guard<std::mutex> lock (*metrics_mutex_);
        std::lock_guard<std::mutex> latency_lock (metrics.latency_mutex);
        metrics.epoch = epoch;
        metrics.active_received = 0;
        metrics.sample_index = 0;
        metrics.latency = latency_sampler_t ();
    }
    return &metrics;
}

template<typename OwnerT>
inline void collect_spot_thread_metrics (
  OwnerT *owner_,
  std::mutex *metrics_mutex_,
  std::vector<spot_thread_metrics_t<OwnerT> *> *thread_metrics_,
  std::atomic<uint64_t> *metrics_epoch_,
  unsigned long long *active_received_out_,
  latency_sampler_stats_t *latency_out_)
{
    if (active_received_out_)
        *active_received_out_ = 0;
    if (latency_out_)
        *latency_out_ = latency_sampler_stats_t ();
    if (!owner_ || !metrics_mutex_ || !thread_metrics_ || !metrics_epoch_)
        return;

    latency_sampler_t merged_latency;
    unsigned long long active_received = 0;
    const uint64_t epoch = metrics_epoch_->load (std::memory_order_acquire);
    {
        std::lock_guard<std::mutex> lock (*metrics_mutex_);
        for (size_t i = 0; i < thread_metrics_->size (); ++i) {
            spot_thread_metrics_t<OwnerT> *metrics = (*thread_metrics_)[i];
            if (!metrics || metrics->owner != owner_ || metrics->epoch != epoch)
                continue;
            active_received += metrics->active_received;
            // Lock per-thread latency while reading its samples vector
            // so a concurrent add() (in the worker hot path) cannot
            // reallocate _samples under our iterators.
            std::lock_guard<std::mutex> latency_lock (metrics->latency_mutex);
            merged_latency.merge_from (metrics->latency);
        }
    }

    if (active_received_out_)
        *active_received_out_ = active_received;
    if (latency_out_)
        *latency_out_ = merged_latency.snapshot ();
}

} // namespace multi
} // namespace perf

#endif
