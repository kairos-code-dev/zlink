/* SPDX-License-Identifier: MPL-2.0 */

#include "../precompiled.hpp"
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO

#include "asio_poller.hpp"

#if !defined ZMQ_HAVE_WINDOWS
#include <unistd.h>
#endif

#include <algorithm>
#include <new>
#include <thread>
#include <chrono>

// Debug logging for ASIO poller - set to 1 to enable
#define ASIO_POLLER_DEBUG 0

#if ASIO_POLLER_DEBUG
#include <cstdio>
#define ASIO_DBG(fmt, ...) fprintf(stderr, "[ASIO_POLLER] " fmt "\n", ##__VA_ARGS__)
#else
#define ASIO_DBG(fmt, ...)
#endif

#include "../macros.hpp"
#include "../err.hpp"
#include "../config.hpp"
#include "../i_poll_events.hpp"

#if !defined ZMQ_HAVE_WINDOWS

zmq::asio_poller_t::poll_entry_t::poll_entry_t (
  boost::asio::io_context &io_ctx_, fd_t fd_) :
    fd (fd_),
    descriptor (io_ctx_, fd_),
    events (NULL),
    pollin_enabled (false),
    pollout_enabled (false),
    in_event_pending (false),
    out_event_pending (false)
{
}

#endif  // !ZMQ_HAVE_WINDOWS

zmq::asio_poller_t::asio_poller_t (const zmq::thread_ctx_t &ctx_) :
    worker_poller_base_t (ctx_),
    _io_context (),
    _work_guard (boost::asio::make_work_guard (_io_context)),
    _stopping (false)
{
    ASIO_DBG ("Constructor called, this=%p", (void *) this);
}

zmq::asio_poller_t::~asio_poller_t ()
{
    //  Wait till the worker thread exits.
    stop_worker ();

    //  Clean up any retired entries
    for (retired_t::iterator it = _retired.begin (), end = _retired.end ();
         it != end; ++it) {
        LIBZMQ_DELETE (*it);
    }
}

zmq::asio_poller_t::handle_t
zmq::asio_poller_t::add_fd (fd_t fd_, i_poll_events *events_)
{
    check_thread ();
    ASIO_DBG ("add_fd: fd=%d", fd_);

#if defined ZMQ_HAVE_WINDOWS
    //  Windows support is not implemented yet in Phase 1-A
    //  TODO: Implement Windows support
    errno_assert (false);
    return NULL;
#else
    poll_entry_t *pe = new (std::nothrow) poll_entry_t (_io_context, fd_);
    alloc_assert (pe);

    pe->events = events_;

    //  Increase the load metric of the thread.
    adjust_load (1);

    ASIO_DBG ("add_fd: done, load=%d", get_load ());
    return pe;
#endif
}

void zmq::asio_poller_t::rm_fd (handle_t handle_)
{
    check_thread ();
    poll_entry_t *pe = static_cast<poll_entry_t *> (handle_);

    //  Cancel any pending async operations
    cancel_ops (pe);

#if !defined ZMQ_HAVE_WINDOWS
    //  Release the native handle so the descriptor won't close the fd
    pe->descriptor.release ();
#endif

    pe->fd = retired_fd;
    _retired.push_back (pe);

    //  Decrease the load metric of the thread.
    adjust_load (-1);
}

void zmq::asio_poller_t::set_pollin (handle_t handle_)
{
    check_thread ();
    poll_entry_t *pe = static_cast<poll_entry_t *> (handle_);
    ASIO_DBG ("set_pollin: fd=%d, pollin_enabled=%d, in_event_pending=%d",
              pe->fd, pe->pollin_enabled, pe->in_event_pending);

    if (!pe->pollin_enabled) {
        pe->pollin_enabled = true;
        if (!pe->in_event_pending) {
            start_wait_read (pe);
        }
    }
}

void zmq::asio_poller_t::reset_pollin (handle_t handle_)
{
    check_thread ();
    poll_entry_t *pe = static_cast<poll_entry_t *> (handle_);

    pe->pollin_enabled = false;
    //  Note: We don't cancel the pending async_wait here.
    //  The callback will check pollin_enabled and ignore the event if disabled.
}

void zmq::asio_poller_t::set_pollout (handle_t handle_)
{
    check_thread ();
    poll_entry_t *pe = static_cast<poll_entry_t *> (handle_);
    ASIO_DBG ("set_pollout: fd=%d, pollout_enabled=%d, out_event_pending=%d",
              pe->fd, pe->pollout_enabled, pe->out_event_pending);

    if (!pe->pollout_enabled) {
        pe->pollout_enabled = true;
        if (!pe->out_event_pending) {
            start_wait_write (pe);
        }
    }
}

void zmq::asio_poller_t::reset_pollout (handle_t handle_)
{
    check_thread ();
    poll_entry_t *pe = static_cast<poll_entry_t *> (handle_);

    pe->pollout_enabled = false;
    //  Note: We don't cancel the pending async_wait here.
    //  The callback will check pollout_enabled and ignore the event if disabled.
}

void zmq::asio_poller_t::stop ()
{
    check_thread ();
}

int zmq::asio_poller_t::max_fds ()
{
    return -1;
}

void zmq::asio_poller_t::start_wait_read (poll_entry_t *entry_)
{
#if !defined ZMQ_HAVE_WINDOWS
    ASIO_DBG ("start_wait_read: fd=%d", entry_->fd);
    entry_->in_event_pending = true;
    entry_->descriptor.async_wait (
      boost::asio::posix::stream_descriptor::wait_read,
      [this, entry_] (const boost::system::error_code &ec) {
          entry_->in_event_pending = false;
          ASIO_DBG ("read callback: fd=%d, ec=%s, pollin_enabled=%d, stopping=%d",
                    entry_->fd, ec.message ().c_str (), entry_->pollin_enabled,
                    _stopping);

          //  Check if the entry has been retired or pollin disabled
          if (ec || entry_->fd == retired_fd || !entry_->pollin_enabled
              || _stopping) {
              ASIO_DBG ("read callback: returning early for fd=%d", entry_->fd);
              return;
          }

          //  Call the event handler
          ASIO_DBG ("read callback: calling in_event() for fd=%d, this=%p",
                    entry_->fd, (void *) this);
          entry_->events->in_event ();
          ASIO_DBG ("read callback: in_event() returned for fd=%d, pollin_enabled=%d, fd_now=%d, this=%p",
                    entry_->fd, entry_->pollin_enabled, entry_->fd, (void *) this);

          //  Re-register for read events if still enabled
          if (entry_->pollin_enabled && entry_->fd != retired_fd
              && !_stopping) {
              start_wait_read (entry_);
          } else {
              ASIO_DBG ("read callback: NOT re-registering for fd=%d (pollin=%d, fd=%d, stopping=%d)",
                        entry_->fd, entry_->pollin_enabled, entry_->fd, _stopping);
          }
      });
#endif
}

void zmq::asio_poller_t::start_wait_write (poll_entry_t *entry_)
{
#if !defined ZMQ_HAVE_WINDOWS
    ASIO_DBG ("start_wait_write: fd=%d", entry_->fd);
    entry_->out_event_pending = true;
    entry_->descriptor.async_wait (
      boost::asio::posix::stream_descriptor::wait_write,
      [this, entry_] (const boost::system::error_code &ec) {
          entry_->out_event_pending = false;
          ASIO_DBG ("write callback: fd=%d, ec=%s, pollout_enabled=%d, stopping=%d",
                    entry_->fd, ec.message ().c_str (), entry_->pollout_enabled,
                    _stopping);

          //  Check if the entry has been retired or pollout disabled
          if (ec || entry_->fd == retired_fd || !entry_->pollout_enabled
              || _stopping) {
              return;
          }

          //  Call the event handler
          ASIO_DBG ("write callback: calling out_event() for fd=%d", entry_->fd);
          entry_->events->out_event ();

          //  Re-register for write events if still enabled
          if (entry_->pollout_enabled && entry_->fd != retired_fd
              && !_stopping) {
              start_wait_write (entry_);
          }
      });
#endif
}

void zmq::asio_poller_t::cancel_ops (poll_entry_t *entry_)
{
#if !defined ZMQ_HAVE_WINDOWS
    boost::system::error_code ec;
    entry_->descriptor.cancel (ec);
    //  Ignore errors from cancel - the descriptor might already be closed
#endif
}

void zmq::asio_poller_t::loop ()
{
    ASIO_DBG ("loop: started, this=%p", (void *) this);

    //  Release the work guard now - we'll rely on pending async operations
    //  to keep the io_context running. This allows run_one() to return
    //  when there are no more pending handlers.
    _work_guard.reset ();

    while (true) {
        //  Execute any due timers.
        uint64_t timeout = execute_timers ();

        int load = get_load ();
        ASIO_DBG ("loop: load=%d, timeout=%llu", load, (unsigned long long) timeout);

        if (load == 0) {
            if (timeout == 0) {
                ASIO_DBG ("loop: exiting (load=0, timeout=0)");
                break;
            }

            //  No FDs registered, but timers pending - sleep until next timer
            //  This should be rare in practice
            ASIO_DBG ("loop: sleeping for %llu ms (no FDs)", (unsigned long long) timeout);
            std::this_thread::sleep_for (
              std::chrono::milliseconds (static_cast<int> (timeout)));
            continue;
        }

        //  Reset the io_context if it's stopped (e.g., after previous run completion)
        if (_io_context.stopped ()) {
            ASIO_DBG ("loop: restarting io_context");
            _io_context.restart ();
        }

        //  Run the io_context for one iteration or until the next timer
        //  We use run_for with a maximum timeout to ensure we periodically
        //  check the load metric, even when no events are ready. This handles
        //  the case where the last FD is removed inside a handler callback.
        static const int max_poll_timeout_ms = 100;
        int poll_timeout_ms;
        if (timeout > 0) {
            poll_timeout_ms = static_cast<int> (
              std::min (timeout, static_cast<uint64_t> (max_poll_timeout_ms)));
        } else {
            poll_timeout_ms = max_poll_timeout_ms;
        }

        ASIO_DBG ("loop: run_for %d ms", poll_timeout_ms);
        _io_context.run_for (
          std::chrono::milliseconds (poll_timeout_ms));

        //  Clean up retired entries that have no pending operations
        for (retired_t::iterator it = _retired.begin (); it != _retired.end ();) {
            poll_entry_t *pe = *it;
            //  Only delete if no pending callbacks
            if (!pe->in_event_pending && !pe->out_event_pending) {
                LIBZMQ_DELETE (pe);
                it = _retired.erase (it);
            } else {
                ++it;
            }
        }
    }

    //  Signal that we're stopping
    _stopping = true;
    ASIO_DBG ("loop: stopping");

    //  Run any remaining handlers (cancelled async ops will fire with error)
    _io_context.poll ();
    ASIO_DBG ("loop: finished");
}

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO
