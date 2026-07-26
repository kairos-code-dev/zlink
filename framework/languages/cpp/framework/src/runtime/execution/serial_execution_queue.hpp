/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "runtime/dispatch/offload_executor.hpp"

#include <zlink/framework/contracts/dispatch/task.hpp>

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace zlink::framework::runtime
{

class serial_execution_queue_t
{
  public:
    using error_handler_t = std::function<void (const std::string &, const std::exception_ptr &)>;
    using async_completion_t = std::function<void (std::function<void ()>)>;
    using async_work_t = std::function<void (async_completion_t)>;

    explicit serial_execution_queue_t (offload_executor_t &executor,
                                       std::size_t capacity = 4096,
                                       error_handler_t error_handler = {},
                                       bool allow_yield = false);
    ~serial_execution_queue_t ();

    serial_execution_queue_t (const serial_execution_queue_t &) = delete;
    serial_execution_queue_t &operator= (const serial_execution_queue_t &) = delete;

    bool try_post (std::string name, std::function<void ()> work);
    bool try_post_async (std::string name, async_work_t work);
    bool try_post_async_front (std::string name, async_work_t work);
    bool try_post_deferred (std::string name, std::function<void ()> work);
    result_t<std::shared_ptr<detail::deferred_barrier_t>>
    reserve_barrier_next (std::string name);
    void post (std::string name, std::function<void ()> work);
    void post_async (std::string name, async_work_t work);
    void run (std::string name, std::function<void ()> work);
    void drain ();
    void close ();
    void cancel_pending ();

    std::size_t pending_count () const;
    bool closed () const;
    bool allows_yield () const noexcept { return _allow_yield; }

  private:
    struct work_item_t
    {
        std::string name;
        async_work_t work;
    };

    void schedule_drain_locked ();
    void drain_loop ();
    void complete_one (std::string name, std::function<void ()> completion);

    offload_executor_t &_executor;
    const std::size_t _capacity;
    const bool _allow_yield;
    error_handler_t _error_handler;
    mutable std::mutex _mutex;
    std::condition_variable _empty;
    std::deque<work_item_t> _queue;
    std::vector<std::pair<std::string, std::function<void ()>>>
      _deferred_after_active;
    std::vector<std::shared_ptr<detail::serial_turn_t>> _active_turns;
    std::vector<std::string> _active_names;
    bool _closed = false;
    bool _drain_scheduled = false;
    bool _draining = false;
    std::size_t _active = 0;
};

} // namespace zlink::framework::runtime
