/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/execution/serial_execution_queue.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <memory>
#include <vector>

namespace zlink::framework::runtime
{

class serial_deferred_barrier_t final : public detail::deferred_barrier_t
{
  public:
    void reached (serial_execution_queue_t::async_completion_t complete)
    {
        std::function<void ()> work;
        {
            std::lock_guard lock (_mutex);
            if (_reached)
                return;
            _reached = true;
            _complete = std::move (complete);
            if (_state == state_t::pending)
                return;
            work = _state == state_t::activated
                     ? std::move (_work)
                     : std::function<void ()>{};
            complete = std::move (_complete);
        }
        finish (std::move (complete), std::move (work));
    }

    result_t<void> activate (std::function<void ()> work) override
    {
        if (!work) {
            return result_t<void>::failure (
              framework_error_kind_t::not_configured,
              "Deferred Actor join barrier work is empty");
        }
        serial_execution_queue_t::async_completion_t complete;
        {
            std::lock_guard lock (_mutex);
            if (_state != state_t::pending) {
                return result_t<void>::failure (
                  framework_error_kind_t::invalid_operation,
                  "Deferred Actor join barrier is already terminal");
            }
            _state = state_t::activated;
            _work = std::move (work);
            if (!_reached)
                return result_t<void>::success ();
            complete = std::move (_complete);
            work = std::move (_work);
        }
        finish (std::move (complete), std::move (work));
        return result_t<void>::success ();
    }

    void cancel () noexcept override
    {
        serial_execution_queue_t::async_completion_t complete;
        {
            std::lock_guard lock (_mutex);
            if (_state != state_t::pending)
                return;
            _state = state_t::cancelled;
            if (!_reached)
                return;
            complete = std::move (_complete);
        }
        try {
            finish (std::move (complete), {});
        }
        catch (...) {
        }
    }

  private:
    enum class state_t
    {
        pending,
        activated,
        cancelled
    };

    static void finish (serial_execution_queue_t::async_completion_t complete,
                        std::function<void ()> work)
    {
        if (!complete)
            return;
        complete ([work = std::move (work)] () mutable {
            if (work)
                work ();
        });
    }

    std::mutex _mutex;
    state_t _state = state_t::pending;
    bool _reached = false;
    serial_execution_queue_t::async_completion_t _complete;
    std::function<void ()> _work;
};

class serial_turn_handle_impl_t final : public detail::serial_turn_t,
                                       public std::enable_shared_from_this<serial_turn_handle_impl_t>
{
  public:
    serial_turn_handle_impl_t (serial_execution_queue_t &queue,
                              std::string name,
                              serial_execution_queue_t::async_completion_t complete) :
        _queue (queue), _name (std::move (name)), _complete (std::move (complete))
    {
    }

    bool release () override
    {
        return finish ([] {});
    }

    bool released () const override
    {
        std::lock_guard lock (_mutex);
        return _released;
    }

    detail::task_scheduler_t resume_scheduler () override
    {
        return [self = shared_from_this ()] (std::function<void ()> work) mutable {
            if (self->_queue.try_post_async (self->_name + "-await-resume",
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
            if (work) {
                work ();
            }
        };
    }

    bool belongs_to (const void *owner) const noexcept override { return owner == &_queue; }
    bool allows_yield () const noexcept override { return _queue.allows_yield (); }

    result_t<void> defer (std::function<void ()> work,
                          std::function<void ()> cancel) override
    {
        if (!work) {
            return result_t<void>::failure (
              framework_error_kind_t::not_configured,
              "Deferred Actor join work is empty");
        }
        std::lock_guard lock (_mutex);
        if (_released) {
            return result_t<void>::failure (
              framework_error_kind_t::not_configured,
              "Actor join defer requires an open Framework handler turn");
        }
        if (_deferred.size () >= 64) {
            return result_t<void>::failure (
              framework_error_kind_t::not_configured,
              "A Framework handler may defer at most 64 Actor joins");
        }
        _deferred.push_back (
          deferred_work_t{std::move (work), std::move (cancel)});
        return result_t<void>::success ();
    }

    void cancel_deferred () noexcept override
    {
        std::vector<deferred_work_t> deferred;
        {
            std::lock_guard lock (_mutex);
            deferred = std::move (_deferred);
            _deferred.clear ();
        }
        cancel_entries (deferred);
    }

    bool complete (std::function<void ()> completion) { return finish (std::move (completion)); }

  private:
    struct deferred_work_t
    {
        std::function<void ()> activate;
        std::function<void ()> cancel;
    };

    bool finish (std::function<void ()> completion)
    {
        serial_execution_queue_t::async_completion_t complete;
        std::vector<deferred_work_t> deferred;
        {
            std::lock_guard lock (_mutex);
            if (_released) {
                return false;
            }
            _released = true;
            complete = std::move (_complete);
            deferred = std::move (_deferred);
        }
        if (!complete) {
            return false;
        }
        complete ([this, completion = std::move (completion),
                   deferred = std::move (deferred)] () mutable {
            try {
                if (completion)
                    completion ();
            }
            catch (...) {
                cancel_entries (deferred);
                throw;
            }
            if (deferred.empty ()) {
                return;
            }
            auto entries = std::make_shared<std::vector<deferred_work_t>> (
              std::move (deferred));
            if (!_queue.try_post_deferred (
                  _name + "-deferred",
                  [entries] () mutable {
                      for (auto &entry : *entries) {
                          if (entry.activate)
                              entry.activate ();
                      }
                  })) {
                cancel_entries (*entries);
            }
        });
        return true;
    }

    static void cancel_entries (std::vector<deferred_work_t> &entries) noexcept
    {
        for (auto &entry : entries) {
            try {
                if (entry.cancel)
                    entry.cancel ();
            }
            catch (...) {
            }
        }
    }

    serial_execution_queue_t &_queue;
    std::string _name;
    mutable std::mutex _mutex;
    serial_execution_queue_t::async_completion_t _complete;
    std::vector<deferred_work_t> _deferred;
    bool _released = false;
};

serial_execution_queue_t::serial_execution_queue_t (offload_executor_t &executor,
                                                    std::size_t capacity,
                                                    error_handler_t error_handler,
                                                    bool allow_yield) :
    _executor (executor), _capacity (capacity), _allow_yield (allow_yield),
    _error_handler (std::move (error_handler))
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
        try {
            work ();
            complete ([] {});
        }
        catch (...) {
            auto error = std::current_exception ();
            complete ([error] { std::rethrow_exception (error); });
        }
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

bool serial_execution_queue_t::post_async_wait (std::string name,
                                                async_work_t work,
                                                std::function<bool ()> stop_requested)
{
    if (!work) {
        throw std::invalid_argument ("serial execution queue work is empty");
    }
    std::unique_lock lock (_mutex);
    _capacity_changed.wait (lock, [&] {
        return _closed || _queue.size () + _active < _capacity
               || (stop_requested && stop_requested ());
    });
    if (_closed || (stop_requested && stop_requested ())) {
        return false;
    }
    _queue.push_back (work_item_t{std::move (name), std::move (work)});
    schedule_drain_locked ();
    return true;
}

bool serial_execution_queue_t::try_post_deferred (
  std::string name,
  std::function<void ()> work)
{
    if (!work) {
        throw std::invalid_argument ("serial execution queue work is empty");
    }
    std::lock_guard<std::mutex> lock (_mutex);
    if (_closed) {
        return false;
    }
    _deferred_after_active.emplace_back (
      std::move (name), std::move (work));
    return true;
}

result_t<std::shared_ptr<detail::deferred_barrier_t>>
serial_execution_queue_t::reserve_barrier_next (std::string name)
{
    auto barrier = std::make_shared<serial_deferred_barrier_t> ();
    if (!try_post_async_front (
          std::move (name),
          [barrier] (auto complete) mutable {
              barrier->reached (std::move (complete));
          })) {
        return result_t<std::shared_ptr<detail::deferred_barrier_t>>::failure (
          framework_error_kind_t::rejected,
          "Deferred Actor join target queue is full or closed");
    }
    return result_t<std::shared_ptr<detail::deferred_barrier_t>>::success (
      std::move (barrier));
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
    _capacity_changed.notify_all ();
}

void serial_execution_queue_t::cancel_pending ()
{
    std::vector<std::shared_ptr<detail::serial_turn_t>> active_turns;
    {
        std::lock_guard<std::mutex> lock (_mutex);
        _closed = true;
        _queue.clear ();
        active_turns = _active_turns;
        _capacity_changed.notify_all ();
    }
    for (auto &turn : active_turns) {
        if (turn) {
            (void) turn->release ();
        }
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
            auto turn = std::make_shared<serial_turn_handle_impl_t> (
              *this, name, [this, name] (std::function<void ()> completion) mutable {
                  bool queue_closed = false;
                  {
                      std::lock_guard<std::mutex> lock (_mutex);
                      queue_closed = _closed;
                  }
                  if (queue_closed) {
                      complete_one (std::move (name), std::move (completion));
                      return;
                  }
                  _executor.submit ([this, name = std::move (name),
                                     completion = std::move (completion)] () mutable {
                      complete_one (std::move (name), std::move (completion));
                  });
              });
            {
                std::lock_guard<std::mutex> lock (_mutex);
                _active_turns.push_back (turn);
                _active_names.push_back (name);
            }
            detail::serial_turn_scope_t scope (turn);
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

    std::vector<std::pair<std::string, std::function<void ()>>>
      deferred_after_active;
    {
        std::lock_guard<std::mutex> lock (_mutex);
        _active_turns.erase (
          std::remove_if (_active_turns.begin (), _active_turns.end (),
                          [] (const auto &turn) { return !turn || turn->released (); }),
          _active_turns.end ());
        _active_names.erase (std::remove (_active_names.begin (), _active_names.end (), name),
                             _active_names.end ());
        --_active;
        _draining = false;
        deferred_after_active =
          std::move (_deferred_after_active);
        _deferred_after_active.clear ();
        if (!_queue.empty ()) {
            schedule_drain_locked ();
        } else if (_queue.empty () && _active == 0) {
            _empty.notify_all ();
        }
        _capacity_changed.notify_all ();
    }
    for (auto &[deferred_name, work] : deferred_after_active) {
        try {
            if (work) {
                work ();
            }
        }
        catch (...) {
            if (_error_handler) {
                _error_handler (
                  deferred_name, std::current_exception ());
            }
        }
    }
}

} // namespace zlink::framework::runtime
