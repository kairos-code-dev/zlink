/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/dispatch/offload_executor.hpp"
#include "runtime/execution/serial_execution_queue.hpp"
#include "runtime/actors/actor_gateway_runtime.hpp"
#include "runtime/spots/spot_runtime.hpp"
#include "runtime/timers/timer_runtime.hpp"

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

struct timer_activation_dependency_t
{
    timer_activation_dependency_t () { ++created; }
    ~timer_activation_dependency_t () { ++destroyed; }

    static inline std::atomic_int created{0};
    static inline std::atomic_int destroyed{0};
};

struct timer_activation_spot_t
{
};

struct timer_activation_handler_t
{
    using dependency_types =
      zlink::framework::dependency_list_t<timer_activation_dependency_t>;

    explicit timer_activation_handler_t (
      timer_activation_dependency_t &dependency) :
        dependency (&dependency)
    {
        ++created;
    }

    ~timer_activation_handler_t () { ++destroyed; }

    zlink::framework::task_t<void>
    handle (timer_activation_spot_t &,
            const zlink::framework::timer_tick_t &)
    {
        auto *expected = static_cast<timer_activation_dependency_t *> (nullptr);
        if (!observed_dependency.compare_exchange_strong (
              expected, dependency)
            && expected != dependency) {
            dependency_mismatch = true;
        }
        ++calls;
        co_return;
    }

    timer_activation_dependency_t *dependency;
    static inline std::atomic_int created{0};
    static inline std::atomic_int destroyed{0};
    static inline std::atomic_int calls{0};
    static inline std::atomic<timer_activation_dependency_t *>
      observed_dependency{nullptr};
    static inline std::atomic_bool dependency_mismatch{false};
};

bool verify_timer_handler_activation_lifetime ()
{
    timer_activation_dependency_t::created = 0;
    timer_activation_dependency_t::destroyed = 0;
    timer_activation_handler_t::created = 0;
    timer_activation_handler_t::destroyed = 0;
    timer_activation_handler_t::calls = 0;
    timer_activation_handler_t::observed_dependency = nullptr;
    timer_activation_handler_t::dependency_mismatch = false;

    zlink::framework::service_collection_t services;
    services.add_scoped<timer_activation_dependency_t> ();
    auto root = services.build_provider ();
    zlink::framework::serializer_registry_t serializers;

    auto run_activation = [&] {
        const auto handlers_before =
          timer_activation_handler_t::created.load ();
        const auto dependencies_before =
          timer_activation_dependency_t::created.load ();
        const auto calls_before =
          timer_activation_handler_t::calls.load ();
        timer_activation_handler_t::observed_dependency = nullptr;
        timer_activation_handler_t::dependency_mismatch = false;
        auto state =
          std::make_shared<zlink::framework::detail::spot_context_state_t> ();
        state->activation_scope =
          std::make_shared<zlink::framework::detail::service_scope_t> (
            zlink::framework::detail::service_scope_t::create (
              root,
              zlink::framework::detail::service_scope_kind_t::spot_activation));
        state->channel_runtime =
          std::make_shared<zlink::framework::detail::channel_runtime_state_t> ();
        state->channel_runtime->serializers = &serializers;
        state->spot_instance =
          std::make_shared<timer_activation_spot_t> ();

        auto context =
          zlink::framework::detail::spot_context_access_t::create (
            state);
        auto first =
          context.add_timer<timer_activation_handler_t> (
            "first", std::chrono::hours (24));
        auto second =
          context.add_timer<timer_activation_handler_t> (
            "second", std::chrono::hours (24));

        auto timer_runtime =
          zlink::framework::detail::timer_runtime_t::from (context);
        const auto first_result =
          timer_runtime.dispatch_fire_count_async (first, 1).result ();
        const auto second_result =
          timer_runtime.dispatch_fire_count_async (second, 1).result ();
        if (!first_result || !second_result) {
            std::cerr << "timer activation dispatch failed: "
                      << static_cast<int> (first_result.error_kind ()) << ", "
                      << static_cast<int> (second_result.error_kind ()) << '\n';
            return false;
        }
        const auto reused =
          timer_activation_handler_t::created.load ()
              == handlers_before + 1
          && timer_activation_dependency_t::created.load ()
               == dependencies_before + 1
          && timer_activation_handler_t::calls.load ()
               >= calls_before + 2
          && timer_activation_handler_t::observed_dependency.load ()
               != nullptr
          && !timer_activation_handler_t::dependency_mismatch.load ();
        if (!reused) {
            std::cerr << "timer activation reuse mismatch: handlers="
                      << timer_activation_handler_t::created.load ()
                      << " deps="
                      << timer_activation_dependency_t::created.load ()
                      << " calls="
                      << timer_activation_handler_t::calls.load ()
                      << '\n';
            return false;
        }

        state->detach_application_instance (false);
        const auto released =
          timer_activation_handler_t::destroyed.load ()
            == timer_activation_handler_t::created.load ()
          && timer_activation_dependency_t::destroyed.load ()
               == timer_activation_dependency_t::created.load ();
        if (!released) {
            std::cerr << "timer activation release mismatch: handlers="
                      << timer_activation_handler_t::created.load () << "/"
                      << timer_activation_handler_t::destroyed.load ()
                      << " deps="
                      << timer_activation_dependency_t::created.load () << "/"
                      << timer_activation_dependency_t::destroyed.load ()
                      << '\n';
        }
        return released;
    };

    if (!run_activation ()
        || timer_activation_handler_t::created.load () != 1) {
        return false;
    }
    if (!run_activation ()
        || timer_activation_handler_t::created.load () != 2) {
        return false;
    }
    return timer_activation_handler_t::destroyed.load () == 2
           && timer_activation_dependency_t::destroyed.load () == 2;
}

bool verify_close_waits_for_timer_callback_barrier ()
{
    const auto handlers_created_before =
      timer_activation_handler_t::created.load ();
    const auto handlers_destroyed_before =
      timer_activation_handler_t::destroyed.load ();
    const auto dependencies_created_before =
      timer_activation_dependency_t::created.load ();
    const auto dependencies_destroyed_before =
      timer_activation_dependency_t::destroyed.load ();

    zlink::framework::service_collection_t services;
    services.add_scoped<timer_activation_dependency_t> ();
    auto root = services.build_provider ();
    auto state =
      std::make_shared<zlink::framework::detail::spot_context_state_t> ();
    state->node =
      std::make_shared<zlink::framework::detail::spot_node_builder_state_t> (
        "timer-close-race");
    state->spot_id = "timer-close-race-spot";
    state->spot_instance =
      std::make_shared<timer_activation_spot_t> ();
    state->activation_scope =
      std::make_shared<zlink::framework::detail::service_scope_t> (
        zlink::framework::detail::service_scope_t::create (
          root,
          zlink::framework::detail::service_scope_kind_t::spot_activation));
    auto handler = std::make_shared<timer_activation_handler_t> (
      state->activation_scope->provider ()
        .get_required<timer_activation_dependency_t> ());
    state->timer_handler_instances.emplace (
      std::type_index (typeid (timer_activation_handler_t)),
      handler);
    handler.reset ();

    auto context =
      zlink::framework::detail::spot_context_access_t::create (
        state);
    if (!state->enter_callback ()) {
        return false;
    }

    const auto first_close = context.close ().result ();
    const auto repeated_close = context.close ().result ();
    if (!first_close || !first_close.value ()
        || !repeated_close || !repeated_close.value ()
        || state->closed
        || timer_activation_handler_t::destroyed.load ()
             != handlers_destroyed_before
        || timer_activation_dependency_t::destroyed.load ()
             != dependencies_destroyed_before) {
        return false;
    }

    state->leave_callback ();
    if (!state->closed || state->activation_scope
        || !state->timer_handler_instances.empty ()
        || timer_activation_handler_t::created.load ()
             != handlers_created_before + 1
        || timer_activation_handler_t::destroyed.load ()
             != handlers_destroyed_before + 1
        || timer_activation_dependency_t::created.load ()
             != dependencies_created_before + 1
        || timer_activation_dependency_t::destroyed.load ()
             != dependencies_destroyed_before + 1) {
        return false;
    }

    const auto after_close = context.close ().result ();
    return after_close && !after_close.value ()
           && timer_activation_handler_t::destroyed.load ()
                == handlers_destroyed_before + 1
           && timer_activation_dependency_t::destroyed.load ()
                == dependencies_destroyed_before + 1;
}

zlink::framework::spot_context_t
context_with_scheduler (const std::shared_ptr<controlled_worker_scheduler_t> &scheduler)
{
    auto state = std::make_shared<zlink::framework::detail::spot_context_state_t> ();
    state->worker_scheduler = scheduler;
    return zlink::framework::detail::spot_context_access_t::create (
      state);
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
    if (!verify_timer_handler_activation_lifetime ()) {
        return 40;
    }
    if (!verify_close_waits_for_timer_callback_barrier ()) {
        return 41;
    }

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

    auto context =
      zlink::framework::detail::spot_context_access_t::create ();
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

        const auto rejected =
          zlink::framework::detail::actor_join_completion_from_erased (
            zlink::framework::detail::actor_join_completion_outcome_t::rejected,
            17, 19, nullptr,
            std::make_optional (zlink::framework::message_t::from (
              std::string ("no"))),
            zlink::framework::framework_error_kind_t::request_failed,
            true);
        const auto *rejected_result =
          std::get_if<zlink::framework::actor_join_rejected_t> (
            &rejected);
        if (rejected_result == nullptr
            || rejected_result->operation_id_high != 17
            || rejected_result->operation_id_low != 19
            || !rejected_result->reply
            || rejected_result->reply->decode<std::string> () != "no") {
            return 33;
        }

        const auto failed =
          zlink::framework::detail::actor_join_completion_from_erased (
            zlink::framework::detail::actor_join_completion_outcome_t::failed,
            23, 29, nullptr, std::nullopt,
            zlink::framework::framework_error_kind_t::request_failed,
            false);
        const auto *failed_result =
          std::get_if<zlink::framework::actor_join_failed_t> (&failed);
        if (failed_result == nullptr
            || failed_result->operation_id_high != 23
            || failed_result->operation_id_low != 29
            || failed_result->error_kind
                 != zlink::framework::framework_error_kind_t::request_failed
            || failed_result->retryable) {
            return 34;
        }

        auto completion_state =
          std::make_shared<
            zlink::framework::detail::spot_node_builder_state_t> (
            "completion-node");
        zlink::framework::detail::spot_node_runtime_t completion_runtime (
          completion_state);
        const zlink::framework::actor_ref_t completion_actor (
          zlink::framework::node_rid_t::from_string ("completion-node"),
          "player", "completion-actor", 7);
        const std::string completion_key = "player:completion-actor";
        auto completion_instance = std::make_shared<int> (42);
        completion_state->actor_instances.emplace (
          completion_key, completion_instance);
        completion_state->actor_generations.emplace (
          completion_key, completion_actor.generation ());
        completion_state->actor_spot_ids.emplace (
          completion_key,
          zlink::framework::spot_id_t ("source-spot"));

        int completion_callback_count = 0;
        bool fail_completion_once = false;
        auto completion_error_kind =
          zlink::framework::framework_error_kind_t::request_failed;
        bool completion_retryable = true;
        std::vector<
          zlink::framework::detail::actor_join_completion_outcome_t>
          completion_outcomes;
        zlink::framework::detail::spot_node_builder_state_t::
          actor_factory_registration_t completion_factory;
        completion_factory.actor_type = std::type_index (typeid (int));
        completion_factory.on_join_completed =
          [&] (void *actor,
               zlink::framework::detail::actor_join_completion_outcome_t outcome,
               std::uint64_t,
               std::uint64_t,
               const zlink::framework::actor_ref_t *,
               const std::optional<zlink::framework::message_t> &,
               zlink::framework::framework_error_kind_t error_kind,
               bool retryable) {
              if (actor != completion_instance.get ()) {
                  return zlink::framework::task_t<void> (
                    zlink::framework::result_t<void>::failure (
                      zlink::framework::framework_error_kind_t::
                        invalid_configuration,
                      "completion callback received another Actor"));
              }
              ++completion_callback_count;
              completion_outcomes.push_back (outcome);
              completion_error_kind = error_kind;
              completion_retryable = retryable;
              if (fail_completion_once) {
                  fail_completion_once = false;
                  return zlink::framework::task_t<void> (
                    zlink::framework::result_t<void>::failure (
                      zlink::framework::framework_error_kind_t::
                        request_failed,
                      "completion callback failed"));
              }
              return zlink::framework::task_t<void> (
                zlink::framework::result_t<void>::success ());
          };
        completion_state->actor_factories.emplace (
          "player", std::move (completion_factory));

        const auto first_operation =
          completion_runtime.actor_join_operation_id ("transfer-1");
        const auto repeated_operation =
          completion_runtime.actor_join_operation_id ("transfer-1");
        const auto second_operation =
          completion_runtime.actor_join_operation_id ("transfer-2");
        if ((first_operation.first == 0 && first_operation.second == 0)
            || first_operation != repeated_operation
            || first_operation == second_operation) {
            return 70;
        }

        const zlink::framework::actor_join_completion_t rejected_completion =
          zlink::framework::actor_join_rejected_t{
            first_operation.first, first_operation.second,
            zlink::framework::message_t::from (
              std::string ("rejected"))};
        if (!completion_runtime.deliver_actor_join_completion (
              completion_actor, rejected_completion,
              zlink::framework::spot_id_t ("source-spot"))
            || !completion_runtime.deliver_actor_join_completion (
              completion_actor, rejected_completion,
              zlink::framework::spot_id_t ("source-spot"))
            || completion_callback_count != 1
            || completion_outcomes
                 != std::vector{
                   zlink::framework::detail::
                     actor_join_completion_outcome_t::rejected}) {
            return 71;
        }

        fail_completion_once = true;
        const zlink::framework::actor_join_completion_t failed_completion =
          zlink::framework::actor_join_failed_t{
            second_operation.first, second_operation.second,
            zlink::framework::framework_error_kind_t::request_failed,
            false};
        if (completion_runtime.deliver_actor_join_completion (
              completion_actor, failed_completion,
              zlink::framework::spot_id_t ("source-spot"))
            || !completion_runtime.deliver_actor_join_completion (
              completion_actor, failed_completion,
              zlink::framework::spot_id_t ("source-spot"))
            || completion_callback_count != 3
            || completion_outcomes.back ()
                 != zlink::framework::detail::
                      actor_join_completion_outcome_t::failed
            || completion_error_kind
                 != zlink::framework::framework_error_kind_t::
                      request_failed
            || completion_retryable) {
            return 72;
        }

        zlink::framework::runtime::offload_executor_t target_executor (1);
        zlink::framework::runtime::serial_execution_queue_t target_queue (
          target_executor, 16);
        std::mutex barrier_events_mutex;
        std::vector<std::string> barrier_events;
        auto record_barrier_event = [&] (std::string event) {
            std::lock_guard lock (barrier_events_mutex);
            barrier_events.push_back (std::move (event));
        };

        deferred_queue.run ("cross-actor-barrier", [&] {
            auto reserved = target_queue.reserve_barrier_next (
              "reserved-join");
            if (!reserved)
                throw std::runtime_error ("target barrier was not reserved");
            const auto barrier = reserved.value ();
            const auto deferred =
              zlink::framework::detail::defer_current_serial_turn (
                [&, barrier] {
                    const auto activated = barrier->activate (
                      [&] { record_barrier_event ("join"); });
                    if (!activated)
                        throw std::runtime_error (
                          "target barrier was not activated");
                },
                [barrier] { barrier->cancel (); });
            if (!deferred)
                throw std::runtime_error ("Join activation was not deferred");
            if (!target_queue.try_post (
                  "queued-actor-turn",
                  [&] { record_barrier_event ("queued"); })) {
                throw std::runtime_error (
                  "target Actor turn was not queued");
            }
            record_barrier_event ("handler");
        });
        target_queue.drain ();
        {
            std::lock_guard lock (barrier_events_mutex);
            if (barrier_events
                != std::vector<std::string>{"handler", "join", "queued"}) {
                return 35;
            }
            barrier_events.clear ();
        }

        deferred_queue.run ("failed-cross-actor-barrier", [&] {
            auto reserved = target_queue.reserve_barrier_next (
              "cancelled-join");
            if (!reserved)
                throw std::runtime_error ("cancelled barrier was not reserved");
            const auto barrier = reserved.value ();
            const auto deferred =
              zlink::framework::detail::defer_current_serial_turn (
                [&, barrier] {
                    const auto activated = barrier->activate ([&] {
                        record_barrier_event ("must-not-run");
                    });
                    if (!activated)
                        throw std::runtime_error (
                          "cancelled barrier unexpectedly failed activation");
                },
                [barrier] { barrier->cancel (); });
            if (!deferred)
                throw std::runtime_error (
                  "cancelled Join activation was not deferred");
            if (!target_queue.try_post (
                  "turn-after-cancelled-join",
                  [&] { record_barrier_event ("after-cancel"); })) {
                throw std::runtime_error (
                  "target Actor turn after cancelled Join was not queued");
            }
            throw std::runtime_error ("handler failed after Join defer");
        });
        target_queue.drain ();
        {
            std::lock_guard lock (barrier_events_mutex);
            if (barrier_events
                != std::vector<std::string>{"after-cancel"}) {
                return 36;
            }
            barrier_events.clear ();
        }

        zlink::framework::detail::actor_gateway_runtime_t actor_gateway;
        zlink::framework::serializer_registry_t actor_serializers;
        actor_gateway.bind_serializers (actor_serializers);
        const zlink::framework::actor_ref_t barrier_actor (
          zlink::framework::node_rid_t::from_string ("barrier-node"),
          "player", "barrier-actor", 1);
        actor_gateway.on_join_spot (
          [&] (const auto &actor, auto, const auto &, auto) {
              record_barrier_event ("production-join");
              return zlink::framework::result_t<
                zlink::framework::detail::actor_join_reply_t>::success (
                  {1, actor, zlink::message_t{}});
          });
        actor_gateway.on_join_barrier (
          [&] (const auto &) {
              return target_queue.reserve_barrier_next (
                "production-join-barrier");
        });
        auto actor_context = actor_gateway.actor_context (barrier_actor);
        try {
            auto serializer_bound_join =
              actor_context.join_spot (
                "serializer-target",
                zlink::framework::message_t::from (
                  std::string ("serializer-probe")));
            (void) serializer_bound_join;
        }
        catch (...) {
            return 37;
        }
        deferred_queue.run ("production-cross-actor-barrier", [&] {
            actor_context.join_spot ("target-spot").defer ();
            if (!target_queue.try_post (
                  "production-queued-turn",
                  [&] { record_barrier_event ("production-queued"); })) {
                throw std::runtime_error (
                  "production target Actor turn was not queued");
            }
            record_barrier_event ("production-handler");
        });
        target_queue.drain ();
        {
            std::lock_guard lock (barrier_events_mutex);
            if (barrier_events
                != std::vector<std::string>{"production-handler",
                                            "production-join",
                                            "production-queued"}) {
                return 37;
            }
            barrier_events.clear ();
        }

        deferred_queue.run ("failed-production-cross-actor-barrier", [&] {
            actor_context.join_spot ("target-spot").defer ();
            if (!target_queue.try_post (
                  "production-turn-after-cancel",
                  [&] { record_barrier_event ("production-after-cancel"); })) {
                throw std::runtime_error (
                  "production post-cancel turn was not queued");
            }
            throw std::runtime_error (
              "production handler failed after Join defer");
        });
        target_queue.drain ();
        {
            std::lock_guard lock (barrier_events_mutex);
            if (barrier_events
                != std::vector<std::string>{"production-after-cancel"}) {
                return 38;
            }
            barrier_events.clear ();
        }

        zlink::framework::runtime::serial_execution_queue_t closing_source_queue (
          deferred_executor, 16);
        closing_source_queue.run ("closing-source-cross-actor-barrier", [&] {
            auto reserved = target_queue.reserve_barrier_next (
              "source-close-cancelled-join");
            if (!reserved)
                throw std::runtime_error (
                  "source-close barrier was not reserved");
            const auto barrier = reserved.value ();
            const auto deferred =
              zlink::framework::detail::defer_current_serial_turn (
                [&, barrier] {
                    const auto activated = barrier->activate ([&] {
                        record_barrier_event ("source-close-must-not-run");
                    });
                    if (!activated)
                        throw std::runtime_error (
                          "source-close barrier activation failed");
                },
                [barrier] { barrier->cancel (); });
            if (!deferred)
                throw std::runtime_error (
                  "source-close Join activation was not deferred");
            if (!target_queue.try_post (
                  "turn-after-source-close",
                  [&] { record_barrier_event ("after-source-close"); })) {
                throw std::runtime_error (
                  "target turn after source close was not queued");
            }
            closing_source_queue.close ();
        });
        target_queue.drain ();
        {
            std::lock_guard lock (barrier_events_mutex);
            if (barrier_events
                != std::vector<std::string>{"after-source-close"}) {
                return 39;
            }
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
