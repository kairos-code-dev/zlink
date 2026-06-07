/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/dispatch/coroutine_executor.hpp"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace zlink::framework::runtime
{

namespace
{

std::mutex &executor_mutex ()
{
    static std::mutex mutex;
    return mutex;
}

std::unique_ptr<coroutine_executor_t> &executor_instance ()
{
    static std::unique_ptr<coroutine_executor_t> executor;
    return executor;
}

std::atomic<coroutine_executor_t *> &executor_fast_path ()
{
    static std::atomic<coroutine_executor_t *> executor{nullptr};
    return executor;
}

std::size_t &configured_worker_count ()
{
    static std::size_t worker_count = 0;
    return worker_count;
}

std::size_t default_worker_count ()
{
    return std::max (1u, std::thread::hardware_concurrency ());
}

} // namespace

coroutine_executor_t::coroutine_executor_t (std::size_t worker_count) :
    _pool (worker_count == 0 ? 1 : worker_count)
{
}

coroutine_executor_t::~coroutine_executor_t ()
{
    drain ();
}

void coroutine_executor_t::drain ()
{
    _pool.stop ();
    _pool.join ();
}

coroutine_executor_t &handler_coroutine_executor ()
{
    auto *ready = executor_fast_path ().load (std::memory_order_acquire);
    if (ready != nullptr) {
        return *ready;
    }
    std::lock_guard lock (executor_mutex ());
    auto &executor = executor_instance ();
    if (!executor) {
        const auto workers =
          configured_worker_count () == 0 ? default_worker_count () : configured_worker_count ();
        executor = std::make_unique<coroutine_executor_t> (workers);
        executor_fast_path ().store (executor.get (), std::memory_order_release);
    }
    return *executor;
}

void configure_handler_coroutine_executor (std::size_t worker_count)
{
    if (worker_count == 0) {
        worker_count = default_worker_count ();
    }
    std::lock_guard lock (executor_mutex ());
    auto &executor = executor_instance ();
    if (executor) {
        return;
    }
    configured_worker_count () = worker_count;
}

} // namespace zlink::framework::runtime
