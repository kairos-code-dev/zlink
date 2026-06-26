/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/execution/serial_execution_queue.hpp"

#include <stdexcept>
#include <utility>
#include <memory>

namespace zlink::framework::runtime
{

class serial_yield_turn_impl_t final :
    public detail::serial_yield_turn_t,
    public std::enable_shared_from_this<serial_yield_turn_impl_t>
{
  public:
    serial_yield_turn_impl_t (serial_execution_queue_t &queue,
                              std::string name,
                              serial_execution_queue_t::async_completion_t complete) :
        _queue (queue),
        _name (std::move (name)),
        _complete (std::move (complete))
    {
    }

    bool release () override { return finish ([] {}); }

    bool released () const override
    {
        std::lock_guard lock (_mutex);
        return _released;
    }

    detail::task_scheduler_t resume_scheduler () override
    {
        return [weak = weak_from_this ()] (std::function<void ()> work) mutable {
            if (auto self = weak.lock ()) {
                if (self->_queue.try_post_async_front (
                      self->_name + "-yield-resume",
                      [work = std::move (work)] (auto complete) mutable {
                          try {
                              work ();
                          }
                          catch (...) {
                          }
                          complete ([] {});
                      })) {
                    return;
                }
            }
            if (work) {
                work ();
            }
        };
    }

    bool complete (std::function<void ()> completion)
    {
        return finish (std::move (completion));
    }

  private:
    bool finish (std::function<void ()> completion)
    {
        serial_execution_queue_t::async_completion_t complete;
        {
            std::lock_guard lock (_mutex);
            if (_released) {
                return false;
            }
            _released = true;
            complete = std::move (_complete);
        }
        if (!complete) {
            return false;
        }
        complete (std::move (completion));
        return true;
    }

    serial_execution_queue_t &_queue;
    std::string _name;
    mutable std::mutex _mutex;
    serial_execution_queue_t::async_completion_t _complete;
    bool _released = false;
};

serial_execution_queue_t::serial_execution_queue_t (offload_executor_t &executor,
                                                    std::size_t capacity,
                                                    error_handler_t error_handler) :
    _executor (executor), _capacity (capacity), _error_handler (std::move (error_handler))
{
    if (_capacity == 0) {
        throw std::invalid_argument ("serial execution queue capacity is zero");
    }
}

serial_execution_queue_t::~serial_execution_queue_t ()
{
    close ();
    drain ();
}

bool serial_execution_queue_t::try_post (std::string name, std::function<void ()> work)
{
    if (!work) {
        throw std::invalid_argument ("serial execution queue work is empty");
    }
    return try_post_async (std::move (name), [work = std::move (work)] (auto complete) mutable {
        complete (std::move (work));
    });
}

bool serial_execution_queue_t::try_post_async (std::string name, async_work_t work)
{
    if (!work) {
        throw std::invalid_argument ("serial execution queue work is empty");
    }
    std::lock_guard<std::mutex> lock (_mutex);
    if (_closed || _queue.size () + _active >= _capacity) {
        return false;
    }
    _queue.push_back (work_item_t{std::move (name), std::move (work)});
    schedule_drain_locked ();
    return true;
}

bool serial_execution_queue_t::try_post_async_front (std::string name, async_work_t work)
{
    if (!work) {
        throw std::invalid_argument ("serial execution queue work is empty");
    }
    std::lock_guard<std::mutex> lock (_mutex);
    if (_closed || _queue.size () + _active >= _capacity) {
        return false;
    }
    _queue.push_front (work_item_t{std::move (name), std::move (work)});
    schedule_drain_locked ();
    return true;
}

void serial_execution_queue_t::post (std::string name, std::function<void ()> work)
{
    if (!try_post (std::move (name), std::move (work))) {
        throw std::runtime_error ("serial execution queue is full or closed");
    }
}

void serial_execution_queue_t::post_async (std::string name, async_work_t work)
{
    if (!try_post_async (std::move (name), std::move (work))) {
        throw std::runtime_error ("serial execution queue is full or closed");
    }
}

void serial_execution_queue_t::run (std::string name, std::function<void ()> work)
{
    post (std::move (name), std::move (work));
    drain ();
}

void serial_execution_queue_t::drain ()
{
    std::unique_lock<std::mutex> lock (_mutex);
    _empty.wait (
      lock, [&] { return _queue.empty () && _active == 0 && !_draining && !_drain_scheduled; });
}

void serial_execution_queue_t::close ()
{
    std::lock_guard<std::mutex> lock (_mutex);
    _closed = true;
    if (_queue.empty () && _active == 0 && !_draining && !_drain_scheduled) {
        _empty.notify_all ();
    }
}

std::size_t serial_execution_queue_t::pending_count () const
{
    std::lock_guard<std::mutex> lock (_mutex);
    return _queue.size () + _active;
}

bool serial_execution_queue_t::closed () const
{
    std::lock_guard<std::mutex> lock (_mutex);
    return _closed;
}

void serial_execution_queue_t::schedule_drain_locked ()
{
    if (_drain_scheduled || _draining) {
        return;
    }
    _drain_scheduled = true;
    _executor.submit ([this] { drain_loop (); });
}

void serial_execution_queue_t::drain_loop ()
{
    for (;;) {
        work_item_t item;
        {
            std::lock_guard<std::mutex> lock (_mutex);
            _drain_scheduled = false;
            if (_queue.empty ()) {
                _draining = false;
                if (_active == 0) {
                    _empty.notify_all ();
                }
                return;
            }
            _draining = true;
            item = std::move (_queue.front ());
            _queue.pop_front ();
            ++_active;
        }

        auto name = item.name;
        try {
            auto turn = std::make_shared<serial_yield_turn_impl_t> (
              *this, name, [this, name] (std::function<void ()> completion) mutable {
                  _executor.submit (
                    [this, name = std::move (name),
                     completion = std::move (completion)] () mutable {
                        complete_one (std::move (name), std::move (completion));
                    });
              });
            detail::serial_yield_turn_scope_t scope (turn);
            item.work ([turn = std::move (turn)] (std::function<void ()> completion) mutable {
                (void) turn->complete (std::move (completion));
            });
        }
        catch (...) {
            auto error = std::current_exception ();
            item.work = {};
            _executor.submit ([this, name = std::move (name), error] () mutable {
                if (_error_handler) {
                    _error_handler (name, error);
                }
                complete_one (std::move (name), [] {});
            });
        }
        return;
    }
}

void serial_execution_queue_t::complete_one (std::string name, std::function<void ()> completion)
{
    try {
        if (completion) {
            completion ();
        }
    }
    catch (...) {
        if (_error_handler) {
            _error_handler (name, std::current_exception ());
        }
    }

    {
        std::lock_guard<std::mutex> lock (_mutex);
        --_active;
        _draining = false;
        if (!_queue.empty ()) {
            schedule_drain_locked ();
        } else if (_queue.empty () && _active == 0) {
            _empty.notify_all ();
        }
    }
}

} // namespace zlink::framework::runtime
