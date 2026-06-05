#ifndef CPP_PERF_SPOT_CLIENT_RECV_HPP
#define CPP_PERF_SPOT_CLIENT_RECV_HPP

#include "perf_spot_thread_metrics.hpp"
#include "perf_common.hpp"

#include <thread>

namespace perf
{
namespace multi
{

template <typename OwnerT> using spot_recv_thread_metrics_t = spot_thread_metrics_t<OwnerT>;

template <typename SlotT> struct spot_recv_worker_t
{
    zlink::poller_t poller;
    std::vector<SlotT *> slots;
    std::vector<zlink::poll_event_t> events;
    std::thread thread;
};

inline size_t resolve_spot_recv_worker_count (size_t slot_count_)
{
    if (slot_count_ == 0)
        return 0;

    if (const char *env = std::getenv ("PERF_MULTI_SPOT_RECV_WORKERS")) {
        const long parsed = std::strtol (env, NULL, 10);
        if (parsed > 0)
            return std::min<size_t> (slot_count_, static_cast<size_t> (parsed));
    }

    // Match the C reference's worker scaling (cpp.md round 19): one worker
    // per ~16 slots, clamped to [4, 128]. The previous policy of one
    // worker per hardware thread oversubscribed cores on machines with
    // higher concurrency than peer-count and burned >3x more CPU than
    // needed without improving throughput.
    const size_t scaled = std::max<size_t> (4u, std::min<size_t> (128u, (slot_count_ + 15u) / 16u));
    return std::min<size_t> (slot_count_, scaled);
}

template <typename OwnerT>
inline spot_recv_thread_metrics_t<OwnerT> *
bind_spot_recv_thread_metrics (OwnerT *owner_,
                               std::mutex *metrics_mutex_,
                               std::vector<spot_recv_thread_metrics_t<OwnerT> *> *thread_metrics_,
                               std::atomic<uint64_t> *metrics_epoch_)
{
    return bind_spot_thread_metrics (owner_, metrics_mutex_, thread_metrics_, metrics_epoch_);
}

template <typename OwnerT>
inline void collect_spot_recv_thread_metrics (OwnerT *owner_,
                                              std::mutex *metrics_mutex_,
                                              std::vector<spot_recv_thread_metrics_t<OwnerT> *> *thread_metrics_,
                                              std::atomic<uint64_t> *metrics_epoch_,
                                              unsigned long long *active_received_out_,
                                              bench_latency_stats_t *latency_out_)
{
    collect_spot_thread_metrics (owner_, metrics_mutex_, thread_metrics_, metrics_epoch_, active_received_out_,
                                 latency_out_);
}

inline void print_spot_client_result_lines (const std::string &lib_,
                                            const char *pattern_,
                                            const std::string &transport_,
                                            size_t msg_size_,
                                            unsigned long long active_count_,
                                            int duration_seconds_,
                                            const bench_latency_stats_t &latency_)
{
    const double throughput =
      static_cast<double> (active_count_) / static_cast<double> (std::max (1, duration_seconds_));
    const double bandwidth = throughput * static_cast<double> (msg_size_) / 1000000.0;

    print_result (lib_, pattern_, transport_, msg_size_, throughput, bandwidth, latency_.mean_ns, latency_.p95_ns,
                  latency_.p99_ns);
}

} // namespace multi
} // namespace perf

#endif
