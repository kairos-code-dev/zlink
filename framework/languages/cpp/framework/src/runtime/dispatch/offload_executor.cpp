/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/dispatch/offload_executor.hpp"

#include <algorithm>
#include <stdexcept>

namespace zlink::framework::runtime
{

offload_executor_t::offload_executor_t (std::size_t worker_count, std::size_t max_queue_length) :
    offload_executor_t (worker_count == 0 ? 1 : worker_count,
                        worker_count == 0 ? 1 : worker_count,
                        max_queue_length,
                        std::chrono::milliseconds {0})
{
}

offload_executor_t::offload_executor_t (std::size_t min_worker_count,
                                        std::size_t max_worker_count,
                                        std::size_t max_queue_length,
                                        std::chrono::milliseconds idle_timeout) :
    _min_worker_count (min_worker_count),
    _max_worker_count (std::max (min_worker_count, max_worker_count)),
    _max_queue_length (max_queue_length),
    _idle_timeout (idle_timeout)
{
    if (_max_worker_count == 0) {
        _max_worker_count = 1;
    }
    _workers.reserve (_max_worker_count);
    for (std::size_t i = 0; i < _min_worker_count; ++i) {
        start_worker_locked ();
    }
}

offload_executor_t::~offload_executor_t ()
{
    drain ();
}

void offload_executor_t::submit (std::function<void ()> work)
{
    if (!try_submit (std::move (work))) {
        throw std::runtime_error ("offload executor queue is full or stopped");
    }
}

bool offload_executor_t::try_submit (std::function<void ()> work)
{
    {
        std::lock_guard lock (_mutex);
        if (_stopping || (_max_queue_length != 0 && _queue.size () >= _max_queue_length)) {
            return false;
        }
        _queue.push (std::move (work));
        if (_idle_workers == 0 && _live_workers < _max_worker_count) {
            start_worker_locked ();
        }
    }
    _ready.notify_one ();
    return true;
}

void offload_executor_t::drain ()
{
    {
        std::unique_lock lock (_mutex);
        _empty.wait (lock, [this] { return _queue.empty () && _active == 0; });
        _stopping = true;
    }
    _ready.notify_all ();
    for (auto &worker : _workers) {
        if (worker.joinable ()) {
            worker.join ();
        }
    }
}

bool offload_executor_t::drained () const
{
    std::lock_guard lock (_mutex);
    return _queue.empty () && _active == 0;
}

std::size_t offload_executor_t::live_worker_count () const
{
    std::lock_guard lock (_mutex);
    return _live_workers;
}

void offload_executor_t::start_worker_locked ()
{
    ++_live_workers;
    _workers.emplace_back ([this] { worker_loop (); });
}

void offload_executor_t::worker_loop ()
{
    while (true) {
        std::function<void ()> work;
        {
            std::unique_lock lock (_mutex);
            ++_idle_workers;
            bool ready = false;
            if (_idle_timeout.count () == 0) {
                _ready.wait (lock, [this] { return _stopping || !_queue.empty (); });
                ready = true;
            } else {
                ready = _ready.wait_for (lock, _idle_timeout, [this] { return _stopping || !_queue.empty (); });
            }
            --_idle_workers;
            if (!ready && _queue.empty () && _live_workers > _min_worker_count) {
                --_live_workers;
                if (_queue.empty () && _active == 0) {
                    _empty.notify_all ();
                }
                return;
            }
            if (_stopping && _queue.empty ()) {
                --_live_workers;
                return;
            }
            work = std::move (_queue.front ());
            _queue.pop ();
            ++_active;
        }

        work ();

        {
            std::lock_guard lock (_mutex);
            --_active;
            if (_queue.empty () && _active == 0) {
                _empty.notify_all ();
            }
        }
    }
}

} // namespace zlink::framework::runtime
