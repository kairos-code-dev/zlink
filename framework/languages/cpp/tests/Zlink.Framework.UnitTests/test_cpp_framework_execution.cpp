/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/dispatch/offload_executor.hpp"
#include "runtime/execution/serial_execution_queue.hpp"
#include "runtime/spots/spot_runtime.hpp"

#include <zlink/framework/contracts/spots/spot.hpp>
#include <zlink/framework/contracts/actors/actor.hpp>

#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <iostream>
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

bool wait_until (const std::function<bool ()> &predicate)
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (predicate ()) {
            return true;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
    return false;
}

zlink::framework::task_t<void> run_request_turn_probe (
  const std::shared_ptr<zlink::framework::detail::task_completion_source_t<int>> &reply,
  const std::shared_ptr<std::vector<int>> &order,
  const std::shared_ptr<std::mutex> &order_gate,
  bool release_turn)
{
    {
        std::lock_guard lock (*order_gate);
        order->push_back (1);
    }
    zlink::framework::request_call_t<int> call (
      "TurnProbe", [reply] (const auto &, auto, const auto &) { return reply->task (); });
    const auto value = release_turn ? co_await call.yield () : co_await call.submit ();
    if (value != 7) {
        throw std::runtime_error ("turn probe reply mismatch");
    }
    {
        std::lock_guard lock (*order_gate);
        order->push_back (3);
    }
    co_return;
}

bool verify_request_turn_mode (bool release_turn, const std::vector<int> &expected)
{
    zlink::framework::runtime::offload_executor_t executor (2);
    zlink::framework::runtime::serial_execution_queue_t queue (
      executor, 4,
      zlink::framework::runtime::serial_execution_queue_t::error_handler_t{},
      true);
    auto reply = std::make_shared<zlink::framework::detail::task_completion_source_t<int>> ();
    auto order = std::make_shared<std::vector<int>> ();
    auto order_gate = std::make_shared<std::mutex> ();

    queue.post_async ("request", [reply, order, order_gate, release_turn] (auto complete) {
        auto task = std::make_shared<zlink::framework::task_t<void>> (
          run_request_turn_probe (reply, order, order_gate, release_turn));
        zlink::framework::observe_task_completion (
          *task, [task, complete = std::move (complete)] (const auto &result) mutable {
              complete ([task, result] {
                  if (!result) {
                      throw std::runtime_error ("turn probe request failed");
                  }
              });
          });
    });
    queue.post ("sibling", [order, order_gate] {
        std::lock_guard lock (*order_gate);
        order->push_back (2);
    });

    if (!wait_until ([&] {
            std::lock_guard lock (*order_gate);
            return release_turn ? order->size () >= 2 : order->size () == 1;
        })) {
        return false;
    }
    if (!release_turn) {
        std::lock_guard lock (*order_gate);
        if (*order != std::vector<int>{1}) {
            return false;
        }
    }
    reply->complete (zlink::framework::result_t<int>::success (7));
    if (!wait_until ([&] {
            std::lock_guard lock (*order_gate);
            return order->size () == 3;
        })) {
        return false;
    }
    queue.drain ();
    std::lock_guard lock (*order_gate);
    if (*order != expected) {
        std::cerr << "turn mode=" << (release_turn ? "yield" : "async") << " order=";
        for (const auto item : *order) {
            std::cerr << item;
        }
        std::cerr << '\n';
    }
    return *order == expected;
}

} // namespace

int main ()
{
    if (!verify_request_turn_mode (false, {1, 3, 2})) {
        return 25;
    }
    if (!verify_request_turn_mode (true, {1, 2, 3})) {
        return 26;
    }

    std::atomic_int unsupported_submit_count = 0;
    zlink::framework::request_call_t<int> unsupported_yield (
      "UnsupportedYield",
      [&unsupported_submit_count] (const auto &, auto, const auto &) {
          unsupported_submit_count.fetch_add (1);
          return zlink::framework::task_t<int> (
            zlink::framework::result_t<int>::success (1));
      });
    const auto unsupported_yield_result = unsupported_yield.yield ().result ();
    if (unsupported_yield_result
        || unsupported_yield_result.error_kind ()
             != zlink::framework::framework_error_kind_t::invalid_configuration
        || unsupported_submit_count.load () != 0) {
        return 30;
    }

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
    auto async_call = context.run_cpu_worker ([] { return 1; });
    auto async_result = async_call.submit ().result ();
    if (async_result
        || async_result.error_kind () != zlink::framework::framework_error_kind_t::request_failed) {
        return 6;
    }
    auto duplicate_async = async_call.submit ().result ();
    if (duplicate_async
        || duplicate_async.error_kind ()
             != zlink::framework::framework_error_kind_t::request_protocol_error) {
        return 7;
    }

    auto callback_call = context.run_cpu_worker ([] { return 2; });
    const auto unconfigured_result = callback_call.submit ().result ();
    if (unconfigured_result
        || unconfigured_result.error_kind ()
             != zlink::framework::framework_error_kind_t::request_failed) {
        return 8;
    }
    const auto duplicate_result = callback_call.submit ().result ();
    if (duplicate_result
        || duplicate_result.error_kind ()
             != zlink::framework::framework_error_kind_t::request_protocol_error) {
        return 9;
    }

    auto scheduler = std::make_shared<controlled_worker_scheduler_t> ();
    auto runtime_context = context_with_scheduler (scheduler);
    auto worker_thread = std::thread::id{};
    auto submit_call = runtime_context.run_cpu_worker ([&] {
        worker_thread = std::this_thread::get_id ();
        return 42;
    });
    auto submit_task = submit_call.submit ();
    if (scheduler->worker_job_count () != 1 || scheduler->owner_job_count () != 0) {
        return 10;
    }
    scheduler->run_worker_job ();
    if (scheduler->owner_job_count () != 1) {
        return 11;
    }
    scheduler->run_owner_job ();
    const auto submit_result = submit_task.result ();
    if (worker_thread == std::thread::id{} || !submit_result || submit_result.value () != 42) {
        return 12;
    }

    auto async_scheduler = std::make_shared<controlled_worker_scheduler_t> ();
    auto async_context = context_with_scheduler (async_scheduler);
    auto worker_call = async_context.run_cpu_worker ([] { return 7; });
    auto worker_task = worker_call.submit ();
    async_scheduler->run_worker_job ();
    async_scheduler->run_owner_job ();
    const auto worker_result = worker_task.result ();
    if (!worker_result || worker_result.value () != 7) {
        return 13;
    }

    auto full_scheduler = std::make_shared<controlled_worker_scheduler_t> ();
    full_scheduler->queue_full = true;
    auto full_context = context_with_scheduler (full_scheduler);
    auto full_call = full_context.run_cpu_worker ([] { return 3; });
    auto full_task = full_call.submit ();
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

    auto timeout_scheduler = std::make_shared<controlled_worker_scheduler_t> ();
    auto timeout_context = context_with_scheduler (timeout_scheduler);
    auto timeout_call = timeout_context.run_cpu_worker ([] { return 9; });
    auto timeout_task = timeout_call.timeout (std::chrono::milliseconds (5)).submit ();
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

    std::vector<std::shared_ptr<zlink::framework::detail::task_completion_source_t<int>>>
      io_sources;
    std::vector<zlink::framework::task_t<int>> io_tasks;
    for (int value = 0; value < 8; ++value) {
        auto source =
          std::make_shared<zlink::framework::detail::task_completion_source_t<int>> ();
        auto call = full_context.run_io_worker ([source] { return source->task (); });
        io_tasks.push_back (call.submit ());
        io_sources.push_back (std::move (source));
    }
    if (full_scheduler->worker_job_count () != 0 || full_scheduler->owner_job_count () != 0) {
        return 27;
    }
    for (int value = 0; value < 8; ++value) {
        io_sources[static_cast<std::size_t> (value)]->complete (
          zlink::framework::result_t<int>::success (value));
    }
    for (int value = 0; value < 8; ++value) {
        const auto io_result = io_tasks[static_cast<std::size_t> (value)].result ();
        if (!io_result || io_result.value () != value) {
            return 28;
        }
    }

    auto io_timeout_source =
      std::make_shared<zlink::framework::detail::task_completion_source_t<int>> ();
    auto io_timeout_call = full_context.run_io_worker (
      [io_timeout_source] { return io_timeout_source->task (); });
    const auto io_timeout_result =
      io_timeout_call.timeout (std::chrono::milliseconds (5)).submit ().result ();
    if (io_timeout_result
        || io_timeout_result.error_kind ()
             != zlink::framework::framework_error_kind_t::worker_timed_out
        || full_scheduler->worker_job_count () != 0) {
        return 29;
    }

    {
        zlink::framework::runtime::offload_executor_t deferred_executor (1);
        zlink::framework::runtime::serial_execution_queue_t deferred_queue (
          deferred_executor, 16);
        std::vector<std::string> events;
        deferred_queue.run ("deferred-actor-join", [&] {
            zlink::framework::actor_join_call_t ([&] (std::chrono::milliseconds) {
                events.push_back ("join");
            }).defer ();
            events.push_back ("handler");
        });
        if (events != std::vector<std::string>{"handler", "join"}) {
            return 30;
        }

        bool detached_rejected = false;
        try {
            zlink::framework::actor_join_call_t (
              [] (std::chrono::milliseconds) {}).defer ();
        }
        catch (const zlink::framework::framework_exception_t &error) {
            detached_rejected =
              error.kind ()
              == zlink::framework::framework_error_kind_t::invalid_configuration;
        }
        if (!detached_rejected) {
            return 31;
        }

        events.clear ();
        deferred_queue.run ("failed-handler", [&] {
            zlink::framework::actor_join_call_t ([&] (std::chrono::milliseconds) {
                events.push_back ("must-not-run");
            }).defer ();
            throw std::runtime_error ("handler failed");
        });
        if (!events.empty ()) {
            return 32;
        }
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
