/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/Contracts/Messaging/operation_contracts.hpp>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace zlink
{
namespace detail
{
namespace
{

constexpr size_t max_queued_async_waits = 4096u;

class async_wait_scheduler_t
{
  public:
    async_wait_scheduler_t ()
    {
        const unsigned int hardware_threads = std::thread::hardware_concurrency ();
        const size_t worker_count = hardware_threads == 0 ? 2u : std::min<size_t> (4u, hardware_threads);
        _workers.reserve (worker_count);
        for (size_t i = 0; i < worker_count; ++i)
            _workers.emplace_back ([this] (std::stop_token stop_) { run (stop_); });
    }

    ~async_wait_scheduler_t ()
    {
        {
            std::lock_guard<std::mutex> lock (_mutex);
            _stopping = true;
        }
        _ready.notify_all ();
        for (std::jthread &worker : _workers)
            worker.request_stop ();
    }

    async_wait_scheduler_t (const async_wait_scheduler_t &) = delete;
    async_wait_scheduler_t &operator= (const async_wait_scheduler_t &) = delete;

    [[nodiscard]] bool schedule (std::function<void ()> task_) noexcept
    {
        try {
            {
                std::lock_guard<std::mutex> lock (_mutex);
                if (_stopping || _tasks.size () >= max_queued_async_waits)
                    return false;
                _tasks.push_back (std::move (task_));
            }
            _ready.notify_one ();
            return true;
        }
        catch (...) {
            return false;
        }
    }

  private:
    void run (std::stop_token stop_)
    {
        while (!stop_.stop_requested ()) {
            std::function<void ()> task;
            {
                std::unique_lock<std::mutex> lock (_mutex);
                _ready.wait (lock, [&] { return stop_.stop_requested () || _stopping || !_tasks.empty (); });
                if (_tasks.empty ())
                    continue;
                task = std::move (_tasks.front ());
                _tasks.pop_front ();
            }
            task ();
        }
    }

    std::mutex _mutex;
    std::condition_variable _ready;
    std::deque<std::function<void ()>> _tasks;
    std::vector<std::jthread> _workers;
    bool _stopping = false;
};

async_wait_scheduler_t &async_wait_scheduler ()
{
    static async_wait_scheduler_t scheduler;
    return scheduler;
}

} // namespace

bool schedule_async_wait (std::function<void ()> task_) noexcept
{
    return async_wait_scheduler ().schedule (std::move (task_));
}

} // namespace detail
} // namespace zlink
