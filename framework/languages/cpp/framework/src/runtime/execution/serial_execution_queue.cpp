/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/execution/serial_execution_queue.hpp"

#include <stdexcept>
#include <utility>

namespace zlink::framework::runtime
{

serial_execution_queue_t::serial_execution_queue_t (
  offload_executor_t &executor,
  std::size_t capacity,
  error_handler_t error_handler)
  : _executor (executor),
    _capacity (capacity),
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

bool
serial_execution_queue_t::try_post (std::string name,
                                    std::function<void ()> work)
{
  if (!work) {
    throw std::invalid_argument ("serial execution queue work is empty");
  }
  std::lock_guard<std::mutex> lock (_mutex);
  if (_closed || _queue.size () + _active >= _capacity) {
    return false;
  }
  _queue.push_back (work_item_t { std::move (name), std::move (work) });
  schedule_drain_locked ();
  return true;
}

void
serial_execution_queue_t::post (std::string name, std::function<void ()> work)
{
  if (!try_post (std::move (name), std::move (work))) {
    throw std::runtime_error ("serial execution queue is full or closed");
  }
}

void
serial_execution_queue_t::run (std::string name, std::function<void ()> work)
{
  post (std::move (name), std::move (work));
  drain ();
}

void
serial_execution_queue_t::drain ()
{
  std::unique_lock<std::mutex> lock (_mutex);
  _empty.wait (lock, [&] {
    return _queue.empty () && _active == 0 && !_draining &&
           !_drain_scheduled;
  });
}

void
serial_execution_queue_t::close ()
{
  std::lock_guard<std::mutex> lock (_mutex);
  _closed = true;
  if (_queue.empty () && _active == 0 && !_draining && !_drain_scheduled) {
    _empty.notify_all ();
  }
}

std::size_t
serial_execution_queue_t::pending_count () const
{
  std::lock_guard<std::mutex> lock (_mutex);
  return _queue.size () + _active;
}

bool
serial_execution_queue_t::closed () const
{
  std::lock_guard<std::mutex> lock (_mutex);
  return _closed;
}

void
serial_execution_queue_t::schedule_drain_locked ()
{
  if (_drain_scheduled || _draining) {
    return;
  }
  _drain_scheduled = true;
  _executor.submit ([this] { drain_loop (); });
}

void
serial_execution_queue_t::drain_loop ()
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

    try {
      item.work ();
    } catch (...) {
      if (_error_handler) {
        _error_handler (item.name, std::current_exception ());
      }
    }
    complete_one ();
  }
}

void
serial_execution_queue_t::complete_one ()
{
  std::lock_guard<std::mutex> lock (_mutex);
  --_active;
  if (_queue.empty () && _active == 0) {
    _draining = false;
    _empty.notify_all ();
  }
}

} // namespace zlink::framework::runtime
