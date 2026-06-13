/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/dispatch/offload_executor.hpp"

#include <stdexcept>

namespace zlink::framework::runtime
{

offload_executor_t::offload_executor_t (std::size_t worker_count, std::size_t max_queue_length) :
    _max_queue_length (max_queue_length)
{
    if (worker_count == 0) {
        worker_count = 1;
    }
    _workers.reserve (worker_count);
    for (std::size_t i = 0; i < worker_count; ++i) {
        _workers.emplace_back ([this] { worker_loop (); });
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

void offload_executor_t::worker_loop ()
{
    while (true) {
        std::function<void ()> work;
        {
            std::unique_lock lock (_mutex);
            _ready.wait (lock, [this] { return _stopping || !_queue.empty (); });
            if (_stopping && _queue.empty ()) {
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
