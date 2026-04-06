#ifndef CPP_PERF_SPOT_CLIENT_RECV_HPP
#define CPP_PERF_SPOT_CLIENT_RECV_HPP

#include "perf_spot_thread_metrics.hpp"
#include "perf_common.hpp"

#include <thread>

namespace perf {
namespace multi {

template<typename OwnerT>
using spot_recv_thread_metrics_t = spot_thread_metrics_t<OwnerT>;

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
    return bind_spot_thread_metrics (
      owner_, metrics_mutex_, thread_metrics_, metrics_epoch_);
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
    collect_spot_thread_metrics (
      owner_,
      metrics_mutex_,
      thread_metrics_,
      metrics_epoch_,
      active_received_out_,
      latency_out_);
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
