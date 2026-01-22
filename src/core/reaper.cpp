/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "utils/macros.hpp"
#include "core/reaper.hpp"
#include "sockets/socket_base.hpp"
#include "utils/err.hpp"

zmq::reaper_t::reaper_t (class ctx_t *ctx_, uint32_t tid_) :
    object_t (ctx_, tid_),
    _poller (NULL),
    _sockets (0),
    _terminating (false)
{
    if (!_mailbox.valid ())
        return;

    _poller = new (std::nothrow) poller_t (*ctx_);
    alloc_assert (_poller);
    _mailbox.set_io_context (&_poller->get_io_context (),
                             &reaper_t::mailbox_handler, this, NULL);
    _mailbox.schedule_if_needed ();

#ifdef HAVE_FORK
    _pid = getpid ();
#endif
}

zmq::reaper_t::~reaper_t ()
{
    LIBZMQ_DELETE (_poller);
}

zmq::mailbox_t *zmq::reaper_t::get_mailbox ()
{
    return &_mailbox;
}

void zmq::reaper_t::start ()
{
    zmq_assert (_mailbox.valid ());

    //  Start the thread.
    _poller->start ("Reaper");
}

void zmq::reaper_t::stop ()
{
    if (get_mailbox ()->valid ()) {
        send_stop ();
    }
}

void zmq::reaper_t::in_event ()
{
    process_mailbox ();
}

void zmq::reaper_t::out_event ()
{
    zmq_assert (false);
}

void zmq::reaper_t::timer_event (int)
{
    zmq_assert (false);
}

void zmq::reaper_t::process_stop ()
{
    _terminating = true;

    //  If there are no sockets being reaped finish immediately.
    if (!_sockets) {
        send_done ();
        _poller->stop ();
    }
}

void zmq::reaper_t::process_reap (socket_base_t *socket_)
{
    //  Add the socket to the poller.
    socket_->start_reaping (_poller);

    ++_sockets;
}

void zmq::reaper_t::process_reaped ()
{
    --_sockets;

    //  If reaped was already asked to terminate and there are no more sockets,
    //  finish immediately.
    if (!_sockets && _terminating) {
        send_done ();
        _poller->stop ();
    }
}

void zmq::reaper_t::process_mailbox ()
{
    do {
#ifdef HAVE_FORK
        if (unlikely (_pid != getpid ())) {
            //printf("zmq::reaper_t::process_mailbox return in child process %d\n", (int)getpid());
            return;
        }
#endif

        while (true) {
            //  Get the next command. If there is none, exit.
            command_t cmd;
            const int rc = _mailbox.recv (&cmd, 0);
            if (rc != 0 && errno == EINTR)
                continue;
            if (rc != 0 && errno == EAGAIN)
                break;
            errno_assert (rc == 0);

            //  Process the command.
            cmd.destination->process_command (cmd);
        }
    } while (_mailbox.reschedule_if_needed ());
}

void zmq::reaper_t::mailbox_handler (void *arg_)
{
    reaper_t *self = static_cast<reaper_t *> (arg_);
    self->process_mailbox ();
}
