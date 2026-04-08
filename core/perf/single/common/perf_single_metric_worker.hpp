#ifndef PERF_SINGLE_METRIC_WORKER_HPP
#define PERF_SINGLE_METRIC_WORKER_HPP

#include "perf_single_latency.hpp"
#include "perf_single_phase.hpp"

#include <atomic>
#include <mutex>
#include <thread>

template <typename StateT>
inline void single_account_metric_event (
  StateT *state_,
  const single_callback_metric_event_t &event_)
{
    if (!state_)
        return;

    if (event_.phase
        == static_cast<uint32_t> (perf_single_metric::phase_active)) {
        const uint64_t now_us = perf_single_metric::now_us ();
        const double latency_us =
          now_us >= event_.sent_ts_us
            ? static_cast<double> (now_us - event_.sent_ts_us)
            : 0.0;
        state_->latency.add (latency_us);
        single_increment_counter (state_->active_processed);
        if (single_phase_wait_notify_armed (*state_))
            state_->cv.notify_all ();
    }
}

template <typename StateT>
struct single_metric_worker_t
{
    single_metric_worker_t () : state (NULL), queue (NULL), stop (false) {}

    StateT *state;
    single_callback_metric_queue_t *queue;
    std::atomic<bool> stop;
    std::thread thread;
};

template <typename StateT>
inline bool start_single_metric_worker (single_metric_worker_t<StateT> *worker_)
{
    if (!worker_ || !worker_->state || !worker_->queue)
        return false;

    worker_->stop.store (false, std::memory_order_release);
    worker_->thread = std::thread ([worker_]() {
        single_callback_metric_event_t event;
        for (;;) {
            {
                std::unique_lock<std::mutex> lock (worker_->state->mutex);
                worker_->state->cv.wait (
                  lock, [worker_]() {
                      return worker_->stop.load (std::memory_order_acquire)
                             || single_load_flag (worker_->state->fatal)
                             || !worker_->queue->empty ();
                  });
            }

            while (worker_->queue->pop (&event))
                single_account_metric_event (worker_->state, event);

            if (worker_->stop.load (std::memory_order_acquire)
                || (single_load_flag (worker_->state->fatal)
                    && worker_->queue->empty ())) {
                break;
            }
        }

        worker_->state->cv.notify_all ();
    });
    return true;
}

template <typename StateT>
inline void stop_single_metric_worker (single_metric_worker_t<StateT> *worker_)
{
    if (!worker_)
        return;
    worker_->stop.store (true, std::memory_order_release);
    if (worker_->state)
        worker_->state->cv.notify_all ();
    if (worker_->thread.joinable ())
        worker_->thread.join ();
}

#endif
