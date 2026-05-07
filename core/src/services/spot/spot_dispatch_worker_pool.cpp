/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_dispatch_worker_pool.hpp"

#include "api/service_spot_request_reply_internal.hpp"

#include <algorithm>
#include <chrono>
#include <errno.h>
#include <thread>

namespace zlink
{
namespace
{
int detected_cpu_count ()
{
    const unsigned int count = std::thread::hardware_concurrency ();
    return count == 0 ? 1 : static_cast<int> (count);
}

const std::chrono::milliseconds dispatch_worker_idle_timeout (1000);
}

spot_dispatch_worker_pool_t::spot_dispatch_worker_pool_t () :
    _stopping (false),
    _min_workers (0),
    _max_workers (0),
    _live_workers (0),
    _idle_workers (0),
    _spawn_failures (0),
    _last_spawn_errno (0)
{
}

spot_dispatch_worker_pool_t::~spot_dispatch_worker_pool_t ()
{
    stop ();
}

int spot_dispatch_worker_pool_t::start (int min_workers_, int max_workers_)
{
    if (min_workers_ < 1 || max_workers_ < min_workers_) {
        errno = EINVAL;
        return -1;
    }

    std::lock_guard<std::mutex> lock (_mutex);
    if (!_threads.empty ())
        return 0;

    _stopping = false;
    _min_workers = min_workers_;
    _max_workers = max_workers_;
    for (int i = 0; i < _min_workers; ++i) {
        if (!spawn_worker_locked ())
            return -1;
    }
    return 0;
}

int spot_dispatch_worker_pool_t::update_limits (int min_workers_,
                                                int max_workers_)
{
    if (min_workers_ < 1 || max_workers_ < min_workers_) {
        errno = EINVAL;
        return -1;
    }

    std::lock_guard<std::mutex> lock (_mutex);
    if (_threads.empty ()) {
        _min_workers = min_workers_;
        _max_workers = max_workers_;
        return 0;
    }

    _min_workers = min_workers_;
    _max_workers = max_workers_;
    while (!_stopping && _live_workers < _min_workers) {
        if (!spawn_worker_locked ())
            return -1;
    }
    _cv.notify_all ();
    return 0;
}

void spot_dispatch_worker_pool_t::stop ()
{
    std::vector<thread_t *> threads;
    {
        std::lock_guard<std::mutex> lock (_mutex);
        _stopping = true;
        _ready.clear ();
        _tasks.clear ();
        _queued.clear ();
        _active.clear ();
        _dirty.clear ();
        threads.swap (_threads);
    }
    _cv.notify_all ();

    for (size_t i = 0; i < threads.size (); ++i) {
        if (threads[i]) {
            threads[i]->stop ();
            delete threads[i];
        }
    }
    {
        std::lock_guard<std::mutex> lock (_mutex);
        _live_workers = 0;
        _idle_workers = 0;
    }
}

int spot_dispatch_worker_pool_t::post (void *spot_)
{
    if (!spot_) {
        errno = EFAULT;
        return -1;
    }

    {
        std::lock_guard<std::mutex> lock (_mutex);
        if (_stopping) {
            errno = ETERM;
            return -1;
        }
        if (_active.find (spot_) != _active.end ()) {
            _dirty.insert (spot_);
        } else if (_queued.insert (spot_).second) {
            _ready.push_back (spot_);
        }
        if (!_ready.empty () && _idle_workers == 0
            && _live_workers < _max_workers) {
            (void) spawn_worker_locked ();
        }
    }
    _cv.notify_one ();
    return 0;
}

int spot_dispatch_worker_pool_t::post_task (task_fn_t fn_, void *arg_)
{
    if (!fn_) {
        errno = EINVAL;
        return -1;
    }

    {
        std::lock_guard<std::mutex> lock (_mutex);
        if (_stopping) {
            errno = ETERM;
            return -1;
        }
        _tasks.push_back (task_t (fn_, arg_));
        if (_idle_workers == 0 && _live_workers < _max_workers) {
            (void) spawn_worker_locked ();
        }
    }
    _cv.notify_one ();
    return 0;
}

int spot_dispatch_worker_pool_t::min_workers () const
{
    std::lock_guard<std::mutex> lock (_mutex);
    return _min_workers;
}

int spot_dispatch_worker_pool_t::max_workers () const
{
    std::lock_guard<std::mutex> lock (_mutex);
    return _max_workers;
}

int spot_dispatch_worker_pool_t::spawn_failures () const
{
    std::lock_guard<std::mutex> lock (_mutex);
    return _spawn_failures;
}

int spot_dispatch_worker_pool_t::last_spawn_errno () const
{
    std::lock_guard<std::mutex> lock (_mutex);
    return _last_spawn_errno;
}

void spot_dispatch_worker_pool_t::worker_entry (void *arg_)
{
    static_cast<spot_dispatch_worker_pool_t *> (arg_)->run_worker ();
}

bool spot_dispatch_worker_pool_t::spawn_worker_locked ()
{
    thread_t *thread = new (std::nothrow) thread_t ();
    if (!thread) {
        errno = ENOMEM;
        ++_spawn_failures;
        _last_spawn_errno = ENOMEM;
        return false;
    }
    _threads.push_back (thread);
    ++_live_workers;
    thread->start (&spot_dispatch_worker_pool_t::worker_entry, this,
                   "spot-dispatch");
    return true;
}

void spot_dispatch_worker_pool_t::run_worker ()
{
    while (true) {
        void *spot = NULL;
        task_t task (NULL, NULL);
        {
            std::unique_lock<std::mutex> lock (_mutex);
            while (!_stopping && _ready.empty () && _tasks.empty ()) {
                ++_idle_workers;
                const std::cv_status status =
                  _cv.wait_for (lock, dispatch_worker_idle_timeout);
                --_idle_workers;
                if (status == std::cv_status::timeout && _ready.empty ()
                    && _tasks.empty () && _live_workers > _min_workers) {
                    --_live_workers;
                    return;
                }
            }
            if (_stopping) {
                --_live_workers;
                return;
            }
            if (!_tasks.empty ()) {
                task = _tasks.front ();
                _tasks.pop_front ();
            } else {
                spot = _ready.front ();
                _ready.pop_front ();
                _queued.erase (spot);
                _active.insert (spot);
            }
        }

        if (task.fn) {
            task.fn (task.arg);
            continue;
        }

        spot_reqrep_internal::run_spot_dispatch_worker_once (spot);

        {
            std::lock_guard<std::mutex> lock (_mutex);
            _active.erase (spot);
            if (!_stopping && _dirty.erase (spot) != 0
                && _queued.insert (spot).second) {
                _ready.push_back (spot);
                _cv.notify_one ();
            }
        }
    }
}

int spot_dispatch_default_workers_min ()
{
    const int cpus = detected_cpu_count ();
    return cpus <= 1 ? 1 : 2;
}

int spot_dispatch_default_workers_max ()
{
    return std::max (1, detected_cpu_count ());
}
}
