/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/dispatch/offload_executor.hpp"
#include "runtime/execution/serial_execution_queue.hpp"
#include "runtime/spots/spot_runtime.hpp"

#include <zlink/framework/contracts/spots/spot.hpp>

#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{

class controlled_worker_scheduler_t final : public zlink::framework::detail::worker_scheduler_t
{
  public:
    bool try_schedule (std::function<void ()> work) override
    {
        if (queue_full) {
            return false;
        }
        std::lock_guard lock (mutex);
        worker_jobs.push (std::move (work));
        return true;
    }

    void post_owner (std::function<void ()> work) override
    {
        std::lock_guard lock (mutex);
        owner_jobs.push (std::move (work));
    }

    void run_worker_job ()
    {
        std::function<void ()> job;
        {
            std::lock_guard lock (mutex);
            job = std::move (worker_jobs.front ());
            worker_jobs.pop ();
        }
        job ();
    }

    void run_owner_job ()
    {
        std::function<void ()> job;
        {
            std::lock_guard lock (mutex);
            job = std::move (owner_jobs.front ());
            owner_jobs.pop ();
        }
        job ();
    }

    std::size_t worker_job_count () const
    {
        std::lock_guard lock (mutex);
        return worker_jobs.size ();
    }

    std::size_t owner_job_count () const
    {
        std::lock_guard lock (mutex);
        return owner_jobs.size ();
    }

    bool queue_full = false;
    mutable std::mutex mutex;
    std::queue<std::function<void ()>> worker_jobs;
    std::queue<std::function<void ()>> owner_jobs;
};

class test_spot_context_t : public zlink::framework::spot_context_t
{
  public:
    explicit test_spot_context_t (
      std::shared_ptr<zlink::framework::detail::spot_context_state_t> state) :
        zlink::framework::spot_context_t (std::move (state))
    {
    }
};

zlink::framework::spot_context_t
context_with_scheduler (const std::shared_ptr<controlled_worker_scheduler_t> &scheduler)
{
    auto state = std::make_shared<zlink::framework::detail::spot_context_state_t> ();
    state->worker_scheduler = scheduler;
    return test_spot_context_t (state);
}

} // namespace

int main ()
{
    zlink::framework::runtime::offload_executor_t executor (2);

    std::vector<int> order;
    std::string failed_item;
    bool error_seen = false;
    zlink::framework::runtime::serial_execution_queue_t queue (
      executor, 4, [&] (const std::string &name, const std::exception_ptr &error) {
          failed_item = name;
          try {
              if (error) {
                  std::rethrow_exception (error);
              }
          }
          catch (const std::runtime_error &ex) {
              error_seen = std::string (ex.what ()) == "boom";
          }
      });

    if (!queue.try_post ("first", [&] { order.push_back (1); })
        || !queue.try_post ("second", [&] { order.push_back (2); })
        || !queue.try_post ("third", [&] { order.push_back (3); })) {
        return 1;
    }
    queue.drain ();
    if (order != std::vector<int>{1, 2, 3} || queue.pending_count () != 0) {
        return 2;
    }

    queue.post ("fail", [] { throw std::runtime_error ("boom"); });
    queue.post ("after-fail", [&] { order.push_back (4); });
    queue.drain ();
    if (!error_seen || failed_item != "fail" || order != std::vector<int>{1, 2, 3, 4}) {
        return 3;
    }

    queue.close ();
    if (!queue.closed () || queue.try_post ("closed", [] {}) || queue.pending_count () != 0) {
        return 4;
    }

    bool capacity_error = false;
    try {
        zlink::framework::runtime::serial_execution_queue_t invalid (executor, 0);
    }
    catch (const std::invalid_argument &) {
        capacity_error = true;
    }
    if (!capacity_error) {
        return 5;
    }

    zlink::framework::spot_context_t context;
    auto async_call = context.run_worker ([] { return 1; });
    auto async_result = async_call.async ().result ();
    if (async_result
        || async_result.error_kind () != zlink::framework::framework_error_kind_t::request_failed) {
        return 6;
    }
    auto duplicate_async = async_call.async ().result ();
    if (duplicate_async
        || duplicate_async.error_kind ()
             != zlink::framework::framework_error_kind_t::request_protocol_error) {
        return 7;
    }

    auto callback_call = context.run_worker ([] { return 2; });
    bool callback_failed = false;
    callback_call.submit ([&] (zlink::framework::result_t<int> result) {
        callback_failed =
          !result
          && result.error_kind () == zlink::framework::framework_error_kind_t::request_failed;
        return zlink::framework::task_t<void> (zlink::framework::result_t<void>::success ());
    });
    if (!callback_failed) {
        return 8;
    }
    bool duplicate_submit_failed = false;
    try {
        callback_call.submit ([] (zlink::framework::result_t<int>) {
            return zlink::framework::task_t<void> (zlink::framework::result_t<void>::success ());
        });
    }
    catch (const zlink::framework::framework_exception_t &error) {
        duplicate_submit_failed =
          error.kind () == zlink::framework::framework_error_kind_t::request_protocol_error;
    }
    if (!duplicate_submit_failed) {
        return 9;
    }

    auto scheduler = std::make_shared<controlled_worker_scheduler_t> ();
    auto runtime_context = context_with_scheduler (scheduler);
    auto worker_thread = std::thread::id{};
    auto owner_thread = std::thread::id{};
    auto submit_call = runtime_context.run_worker ([&] {
        worker_thread = std::this_thread::get_id ();
        return 42;
    });
    submit_call.submit ([&] (zlink::framework::result_t<int> result) {
        owner_thread = std::this_thread::get_id ();
        if (!result || result.value () != 42) {
            return zlink::framework::task_t<void> (zlink::framework::result_t<void>::failure (
              zlink::framework::framework_error_kind_t::request_failed,
              "worker callback result mismatch"));
        }
        return zlink::framework::task_t<void> (zlink::framework::result_t<void>::success ());
    });
    if (scheduler->worker_job_count () != 1 || scheduler->owner_job_count () != 0) {
        return 10;
    }
    scheduler->run_worker_job ();
    if (owner_thread != std::thread::id{} || scheduler->owner_job_count () != 1) {
        return 11;
    }
    scheduler->run_owner_job ();
    if (worker_thread == std::thread::id{} || owner_thread == std::thread::id{}) {
        return 12;
    }

    auto async_scheduler = std::make_shared<controlled_worker_scheduler_t> ();
    auto async_context = context_with_scheduler (async_scheduler);
    auto worker_call = async_context.run_worker ([] { return 7; });
    auto worker_task = worker_call.async ();
    async_scheduler->run_worker_job ();
    async_scheduler->run_owner_job ();
    const auto worker_result = worker_task.result ();
    if (!worker_result || worker_result.value () != 7) {
        return 13;
    }

    auto full_scheduler = std::make_shared<controlled_worker_scheduler_t> ();
    full_scheduler->queue_full = true;
    auto full_context = context_with_scheduler (full_scheduler);
    auto full_call = full_context.run_worker ([] { return 3; });
    auto full_task = full_call.async ();
    if (full_scheduler->worker_job_count () != 0 || full_scheduler->owner_job_count () != 1) {
        return 14;
    }
    full_scheduler->run_owner_job ();
    const auto full_result = full_task.result ();
    if (full_result
        || full_result.error_kind ()
             != zlink::framework::framework_error_kind_t::worker_queue_full) {
        return 15;
    }

    auto full_callback_scheduler = std::make_shared<controlled_worker_scheduler_t> ();
    full_callback_scheduler->queue_full = true;
    auto full_callback_context = context_with_scheduler (full_callback_scheduler);
    bool full_callback_seen = false;
    auto full_callback_call = full_callback_context.run_worker ([] { return 4; });
    full_callback_call.submit ([&] (zlink::framework::result_t<int> result) {
        full_callback_seen =
          !result
          && result.error_kind () == zlink::framework::framework_error_kind_t::worker_queue_full;
        return zlink::framework::task_t<void> (zlink::framework::result_t<void>::success ());
    });
    if (full_callback_seen || full_callback_scheduler->worker_job_count () != 0
        || full_callback_scheduler->owner_job_count () != 1) {
        return 16;
    }
    full_callback_scheduler->run_owner_job ();
    if (!full_callback_seen) {
        return 17;
    }

    auto timeout_scheduler = std::make_shared<controlled_worker_scheduler_t> ();
    auto timeout_context = context_with_scheduler (timeout_scheduler);
    auto timeout_call = timeout_context.run_worker ([] { return 9; });
    auto timeout_task = timeout_call.timeout (std::chrono::milliseconds (5)).async ();
    for (int attempt = 0; attempt < 50 && timeout_scheduler->owner_job_count () == 0; ++attempt) {
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
    if (timeout_scheduler->worker_job_count () != 1 || timeout_scheduler->owner_job_count () != 1) {
        return 18;
    }
    timeout_scheduler->run_owner_job ();
    const auto timeout_result = timeout_task.result ();
    if (timeout_result
        || timeout_result.error_kind ()
             != zlink::framework::framework_error_kind_t::worker_timed_out) {
        return 19;
    }
    timeout_scheduler->run_worker_job ();
    if (timeout_scheduler->owner_job_count () != 0) {
        return 20;
    }

    zlink::framework::runtime::offload_executor_t elastic_executor (
      0, 2, 8, std::chrono::milliseconds (5));
    if (elastic_executor.live_worker_count () != 0) {
        return 21;
    }
    std::atomic_bool elastic_work_ran = false;
    if (!elastic_executor.try_submit ([&] { elastic_work_ran.store (true); })) {
        return 22;
    }
    for (int attempt = 0; attempt < 50 && !elastic_work_ran.load (); ++attempt) {
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
    if (!elastic_work_ran.load ()) {
        return 23;
    }
    for (int attempt = 0; attempt < 50 && elastic_executor.live_worker_count () != 0; ++attempt) {
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
    if (elastic_executor.live_worker_count () != 0) {
        return 24;
    }

    return 0;
}
