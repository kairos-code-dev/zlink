/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <thread>

#include "api/request_timeout_scheduler_internal.hpp"

namespace zlink
{
namespace request_timeout
{
struct task_t
{
    task_t () :
        registered (false),
        canceled (false),
        firing (false),
        completed (false),
        deadline_ns (0),
        handler (NULL),
        userdata (NULL)
    {
    }

    std::mutex mutex;
    std::condition_variable cv;
    bool registered;
    bool canceled;
    bool firing;
    bool completed;
    uint64_t deadline_ns;
    handler_fn handler;
    void *userdata;
};

namespace
{
typedef std::multimap<uint64_t, std::shared_ptr<task_t> > schedule_map_t;

std::mutex g_timeout_mutex;
std::condition_variable g_timeout_cv;
schedule_map_t g_timeout_schedule;
std::thread g_timeout_thread;
bool g_timeout_started = false;

uint64_t monotonic_now_ns ()
{
    return static_cast<uint64_t> (
      std::chrono::duration_cast<std::chrono::nanoseconds> (
        std::chrono::steady_clock::now ().time_since_epoch ())
        .count ());
}

void run_timeout_loop ()
{
    for (;;) {
        std::shared_ptr<task_t> task;
        {
            std::unique_lock<std::mutex> lock (g_timeout_mutex);
            while (g_timeout_schedule.empty ())
                g_timeout_cv.wait (lock);

            for (;;) {
                if (g_timeout_schedule.empty ())
                    break;

                const uint64_t now_ns = monotonic_now_ns ();
                const schedule_map_t::iterator next = g_timeout_schedule.begin ();
                if (next->first > now_ns) {
                    g_timeout_cv.wait_for (
                      lock, std::chrono::nanoseconds (next->first - now_ns));
                    continue;
                }

                task = next->second;
                g_timeout_schedule.erase (next);
                break;
            }
        }

        if (!task)
            continue;

        handler_fn handler = NULL;
        void *userdata = NULL;
        {
            std::lock_guard<std::mutex> lock (task->mutex);
            task->registered = false;
            if (!task->canceled && !task->completed) {
                task->firing = true;
                handler = task->handler;
                userdata = task->userdata;
            }
        }

        if (handler)
            handler (userdata);

        {
            std::lock_guard<std::mutex> lock (task->mutex);
            task->firing = false;
            task->completed = true;
            task->cv.notify_all ();
        }
    }
}

void ensure_started ()
{
    std::lock_guard<std::mutex> lock (g_timeout_mutex);
    if (g_timeout_started)
        return;
    g_timeout_thread = std::thread (run_timeout_loop);
    g_timeout_thread.detach ();
    g_timeout_started = true;
}
}

std::shared_ptr<task_t> schedule (uint32_t timeout_ms_,
                                  handler_fn handler_,
                                  void *userdata_)
{
    if (timeout_ms_ == 0 || !handler_)
        return std::shared_ptr<task_t> ();

    ensure_started ();

    std::shared_ptr<task_t> task (new task_t ());
    task->handler = handler_;
    task->userdata = userdata_;
    task->deadline_ns =
      monotonic_now_ns ()
      + static_cast<uint64_t> (timeout_ms_) * static_cast<uint64_t> (1000000);
    task->registered = true;

    {
        std::lock_guard<std::mutex> lock (g_timeout_mutex);
        g_timeout_schedule.insert (std::make_pair (task->deadline_ns, task));
    }
    g_timeout_cv.notify_all ();
    return task;
}

void cancel (const std::shared_ptr<task_t> &task_)
{
    if (!task_)
        return;

    {
        std::lock_guard<std::mutex> schedule_lock (g_timeout_mutex);
        if (task_->registered) {
            const schedule_map_t::iterator begin =
              g_timeout_schedule.lower_bound (task_->deadline_ns);
            const schedule_map_t::iterator end =
              g_timeout_schedule.upper_bound (task_->deadline_ns);
            for (schedule_map_t::iterator it = begin; it != end; ++it) {
                if (it->second == task_) {
                    g_timeout_schedule.erase (it);
                    break;
                }
            }
            task_->registered = false;
        }
    }

    std::unique_lock<std::mutex> lock (task_->mutex);
    task_->canceled = true;
    while (task_->firing)
        task_->cv.wait (lock);
    task_->completed = true;
}
}
}
