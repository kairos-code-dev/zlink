#ifndef CPP_PERF_SPOT_CLIENT_CALLBACK_HPP
#define CPP_PERF_SPOT_CLIENT_CALLBACK_HPP

#include "perf_spot_thread_metrics.hpp"
#include "perf_common.hpp"

namespace perf {
namespace multi {

template<typename OwnerT>
using spot_callback_thread_metrics_t = spot_thread_metrics_t<OwnerT>;

template<typename OwnerT>
inline spot_callback_thread_metrics_t<OwnerT> *bind_spot_callback_thread_metrics (
  OwnerT *owner_,
  std::mutex *metrics_mutex_,
  std::vector<spot_callback_thread_metrics_t<OwnerT> *> *thread_metrics_,
  std::atomic<uint64_t> *metrics_epoch_)
{
    return bind_spot_thread_metrics (
      owner_, metrics_mutex_, thread_metrics_, metrics_epoch_);
}

template<typename OwnerT>
inline void collect_spot_callback_thread_metrics (
  OwnerT *owner_,
  std::mutex *metrics_mutex_,
  std::vector<spot_callback_thread_metrics_t<OwnerT> *> *thread_metrics_,
  std::atomic<uint64_t> *metrics_epoch_,
  unsigned long long *active_received_out_,
  bench_latency_stats_t *latency_out_)
{
    collect_spot_thread_metrics (
      owner_,
      metrics_mutex_,
      thread_metrics_,
      metrics_epoch_,
      active_received_out_,
      latency_out_);
}

} // namespace multi
} // namespace perf

#endif
