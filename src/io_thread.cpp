/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include <new>

#include "macros.hpp"
#include "io_thread.hpp"
#include "err.hpp"
#include "ctx.hpp"

zmq::io_thread_t::io_thread_t (ctx_t *ctx_, uint32_t tid_) :
    object_t (ctx_, tid_)
{
    _poller = new (std::nothrow) poller_t (*ctx_);
    alloc_assert (_poller);
    _mailbox.set_io_context (&_poller->get_io_context (),
                             &io_thread_t::mailbox_handler, this, NULL);
    _mailbox.schedule_if_needed ();
}

zmq::io_thread_t::~io_thread_t ()
{
    LIBZMQ_DELETE (_poller);
}

void zmq::io_thread_t::start ()
{
    char name[16] = "";
    snprintf (name, sizeof (name), "IO/%u",
              get_tid () - zmq::ctx_t::reaper_tid - 1);
    //  Start the underlying I/O thread.
    _poller->start (name);
}

void zmq::io_thread_t::stop ()
{
    send_stop ();
}

zmq::mailbox_t *zmq::io_thread_t::get_mailbox ()
{
    return &_mailbox;
}

int zmq::io_thread_t::get_load () const
{
    return _poller->get_load ();
}

void zmq::io_thread_t::in_event ()
{
    process_mailbox ();
}

void zmq::io_thread_t::process_mailbox ()
{
    //  TODO: Do we want to limit number of commands I/O thread can
    //  process in a single go?

    do {
        command_t cmd;
        int rc = _mailbox.recv (&cmd, 0);

        while (rc == 0 || errno == EINTR) {
            if (rc == 0)
                cmd.destination->process_command (cmd);
            rc = _mailbox.recv (&cmd, 0);
        }

        errno_assert (rc != 0 && errno == EAGAIN);
    } while (_mailbox.reschedule_if_needed ());
}

void zmq::io_thread_t::out_event ()
{
    //  We are never polling for POLLOUT here. This function is never called.
    zmq_assert (false);
}

void zmq::io_thread_t::timer_event (int)
{
    //  No timers here. This function is never called.
    zmq_assert (false);
}

zmq::poller_t *zmq::io_thread_t::get_poller () const
{
    zmq_assert (_poller);
    return _poller;
}

boost::asio::io_context &zmq::io_thread_t::get_io_context () const
{
    zmq_assert (_poller);
    return _poller->get_io_context ();
}

void zmq::io_thread_t::process_stop ()
{
    _poller->stop ();
}

void zmq::io_thread_t::mailbox_handler (void *arg_)
{
    io_thread_t *self = static_cast<io_thread_t *> (arg_);
    self->process_mailbox ();
}
