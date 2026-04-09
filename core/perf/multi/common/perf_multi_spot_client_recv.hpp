#ifndef PERF_MULTI_SPOT_CLIENT_RECV_HPP
#define PERF_MULTI_SPOT_CLIENT_RECV_HPP

#include "perf_common_multi.hpp"
#include "perf_multi_metric_header.hpp"
#include "../common/perf_multi_client_helpers.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace perf_multi_spot_client_recv {

template<typename OwnerT>
struct thread_metrics_t
{
    thread_metrics_t() :
        owner(NULL),
        registered(false),
        epoch(0),
        active_received(0),
        sample_index(0)
    {
    }

    OwnerT *owner;
    bool registered;
    uint64_t epoch;
    unsigned long long active_received;
    unsigned long long sample_index;
    bench_latency_sampler_t latency;
};

inline size_t resolve_recv_worker_count(size_t slot_count)
{
    if (slot_count == 0)
        return 0;

    const size_t configured = static_cast<size_t>(
      resolve_multi_int_env("PERF_MULTI_SPOT_RECV_WORKERS", 0, 0));
    if (configured > 0)
        return std::min(slot_count, configured);

    const size_t scaled =
      std::max<size_t>(4, std::min<size_t>(128, (slot_count + 15) / 16));
    return std::min(slot_count, scaled);
}

template<typename OwnerT>
inline void reset_thread_metrics(thread_metrics_t<OwnerT> *metrics,
                                 uint64_t epoch)
{
    if (!metrics)
        return;

    metrics->epoch = epoch;
    metrics->active_received = 0;
    metrics->sample_index = 0;
    metrics->latency.reset();
}

template<typename OwnerT>
inline thread_metrics_t<OwnerT> *bind_thread_metrics(
  OwnerT *owner,
  std::mutex *metrics_mutex,
  std::vector<thread_metrics_t<OwnerT> *> *thread_metrics,
  std::atomic<uint64_t> *metrics_epoch)
{
    if (!owner || !metrics_mutex || !thread_metrics || !metrics_epoch)
        return NULL;

    static thread_local thread_metrics_t<OwnerT> metrics;
    if (!metrics.registered || metrics.owner != owner) {
        metrics.owner = owner;
        metrics.registered = true;
        std::lock_guard<std::mutex> lock(*metrics_mutex);
        if (std::find(thread_metrics->begin(), thread_metrics->end(), &metrics)
            == thread_metrics->end()) {
            thread_metrics->push_back(&metrics);
        }
    }

    const uint64_t epoch = metrics_epoch->load(std::memory_order_acquire);
    if (metrics.epoch != epoch)
        reset_thread_metrics(&metrics, epoch);
    return &metrics;
}

template<typename OwnerT>
inline void collect_thread_metrics(
  OwnerT *owner,
  std::mutex *metrics_mutex,
  std::vector<thread_metrics_t<OwnerT> *> *thread_metrics,
  std::atomic<uint64_t> *metrics_epoch,
  unsigned long long *active_received_out,
  bench_latency_stats_t *latency_out)
{
    if (active_received_out)
        *active_received_out = 0;
    if (latency_out)
        *latency_out = bench_latency_stats_t();
    if (!owner || !metrics_mutex || !thread_metrics || !metrics_epoch)
        return;

    const uint64_t epoch = metrics_epoch->load(std::memory_order_acquire);
    bench_latency_sampler_t merged_latency;
    unsigned long long active_received = 0;
    {
        std::lock_guard<std::mutex> lock(*metrics_mutex);
        for (size_t i = 0; i < thread_metrics->size(); ++i) {
            thread_metrics_t<OwnerT> *metrics = (*thread_metrics)[i];
            if (!metrics || metrics->owner != owner || metrics->epoch != epoch)
                continue;
            active_received += metrics->active_received;
            merged_latency.merge_from(metrics->latency);
        }
    }

    if (active_received_out)
        *active_received_out = active_received;
    if (latency_out)
        *latency_out = merged_latency.snapshot();
}

inline bool emit_client_result_lines(const char *pattern,
                                     const std::string &lib_name,
                                     const std::string &transport,
                                     size_t msg_size,
                                     int duration_seconds,
                                     bool fatal,
                                     unsigned long long active_received,
                                     const bench_latency_stats_t &latency)
{
    if (!pattern || duration_seconds <= 0 || fatal || active_received == 0
        || latency.mean_ns <= 0.0) {
        errno = EINVAL;
        return false;
    }

    const double throughput =
      static_cast<double>(active_received)
      / static_cast<double>(duration_seconds);
    perf_multi_client::print_client_result_lines(
      pattern, lib_name, transport, msg_size, throughput, latency);
    return true;
}

template<typename StateT, typename WorkerT, typename LoopFn>
inline bool start_recv_workers(StateT *state, const LoopFn &loop_fn)
{
    if (!state || state->slots.empty())
        return false;

    const size_t worker_count = resolve_recv_worker_count(state->slots.size());
    if (worker_count == 0)
        return false;

    state->recv_workers_stop.store(false, std::memory_order_release);
    state->recv_workers.reserve(worker_count);
    for (size_t i = 0; i < worker_count; ++i) {
        WorkerT *worker = new (std::nothrow) WorkerT();
        if (!worker)
            return false;
        worker->state = state;
        worker->poller = zlink_poller_new();
        if (!worker->poller) {
            delete worker;
            return false;
        }
        state->recv_workers.push_back(worker);
    }

    for (size_t i = 0; i < state->slots.size(); ++i) {
        typename StateT::slot_type *slot = state->slots[i];
        WorkerT *worker = state->recv_workers[i % worker_count];
        if (!slot || !worker || !worker->poller || !slot->handle
            || zlink_poller_add(worker->poller, slot->handle, slot, ZLINK_POLLIN)
                 != 0) {
            return false;
        }
        worker->slots.push_back(slot);
    }

    for (size_t i = 0; i < state->recv_workers.size(); ++i) {
        WorkerT *worker = state->recv_workers[i];
        if (!worker || worker->slots.empty())
            continue;
        worker->events.resize(worker->slots.size());
        worker->thread = std::thread(loop_fn, worker);
    }

    return true;
}

template<typename StateT, typename WorkerT>
inline void stop_recv_workers(StateT *state)
{
    if (!state)
        return;

    state->recv_workers_stop.store(true, std::memory_order_release);
    for (size_t i = 0; i < state->recv_workers.size(); ++i) {
        WorkerT *worker = state->recv_workers[i];
        if (!worker)
            continue;
        if (worker->thread.joinable())
            worker->thread.join();
        if (worker->poller)
            zlink_poller_destroy(&worker->poller);
        delete worker;
    }
    state->recv_workers.clear();
}

} // namespace perf_multi_spot_client_recv

#endif
