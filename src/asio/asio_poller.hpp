/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZMQ_ASIO_POLLER_HPP_INCLUDED__
#define __ZMQ_ASIO_POLLER_HPP_INCLUDED__

//  poller.hpp decides which polling mechanism to use.
#include "../poller.hpp"
#if defined ZMQ_IOTHREAD_POLLER_USE_ASIO

#include <vector>
#include <map>

#include <boost/asio.hpp>
#if !defined ZMQ_HAVE_WINDOWS
#include <boost/asio/posix/stream_descriptor.hpp>
#endif

#include "../ctx.hpp"
#include "../fd.hpp"
#include "../thread.hpp"
#include "../poller_base.hpp"

namespace zmq
{
struct i_poll_events;

//  This class implements socket polling mechanism using Boost.Asio.
//  It uses the reactor pattern via async_wait to maintain compatibility
//  with existing zmq engine code.

class asio_poller_t ZMQ_FINAL : public worker_poller_base_t
{
  public:
    typedef void *handle_t;

    asio_poller_t (const thread_ctx_t &ctx_);
    ~asio_poller_t () ZMQ_OVERRIDE;

    //  "poller" concept.
    handle_t add_fd (fd_t fd_, zmq::i_poll_events *events_);
    void rm_fd (handle_t handle_);
    void set_pollin (handle_t handle_);
    void reset_pollin (handle_t handle_);
    void set_pollout (handle_t handle_);
    void reset_pollout (handle_t handle_);
    void stop ();

    static int max_fds ();

  private:
    //  Main event loop.
    void loop () ZMQ_OVERRIDE;

    //  Poll entry structure for tracking FD state
    struct poll_entry_t
    {
        fd_t fd;
#if defined ZMQ_HAVE_WINDOWS
        // Windows: Use a different approach since stream_descriptor is POSIX-only
        // For now, we'll need to implement a Windows-specific solution
        // TODO: Implement Windows support using IOCP or overlapped I/O
#else
        boost::asio::posix::stream_descriptor descriptor;
#endif
        zmq::i_poll_events *events;
        bool pollin_enabled;
        bool pollout_enabled;
        bool in_event_pending;
        bool out_event_pending;

#if !defined ZMQ_HAVE_WINDOWS
        poll_entry_t (boost::asio::io_context &io_ctx_, fd_t fd_);
#endif
    };

    //  Start async_wait for read readiness
    void start_wait_read (poll_entry_t *entry_);

    //  Start async_wait for write readiness
    void start_wait_write (poll_entry_t *entry_);

    //  Cancel pending async operations for an entry
    void cancel_ops (poll_entry_t *entry_);

    //  The Asio io_context that drives the event loop
    boost::asio::io_context _io_context;

    //  Work guard to keep io_context running even when no handlers are pending
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
      _work_guard;

    //  List of retired event sources (to be deleted after loop iteration)
    typedef std::vector<poll_entry_t *> retired_t;
    retired_t _retired;

    //  Flag to track stopping state
    bool _stopping;

    ZMQ_NON_COPYABLE_NOR_MOVABLE (asio_poller_t)
};

typedef asio_poller_t poller_t;
}

#endif  // ZMQ_IOTHREAD_POLLER_USE_ASIO

#endif  // __ZMQ_ASIO_POLLER_HPP_INCLUDED__
