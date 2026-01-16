/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#include "mailbox.hpp"
#include "err.hpp"

#include <boost/asio.hpp>

zmq::mailbox_t::mailbox_t ()
{
    const bool ok = _cpipe.check_read ();
    zmq_assert (!ok);
    _io_context = NULL;
    _handler = NULL;
    _handler_arg = NULL;
    _pre_post = NULL;
    _scheduled.store (false, std::memory_order_release);
}

zmq::mailbox_t::~mailbox_t ()
{
    //  TODO: Retrieve and deallocate commands inside the _cpipe.

    // Work around problem that other threads might still be in our
    // send() method, by waiting on the mutex before disappearing.
    _sync.lock ();
    _sync.unlock ();
}

void zmq::mailbox_t::send (const command_t &cmd_)
{
    _sync.lock ();
    _cpipe.write (cmd_, false);
    _cpipe.flush ();
    _cond_var.broadcast ();
    _sync.unlock ();

    schedule_if_needed ();
}

int zmq::mailbox_t::recv (command_t *cmd_, int timeout_)
{
    _sync.lock ();

    if (_cpipe.read (cmd_)) {
        _sync.unlock ();
        return 0;
    }

    if (timeout_ == 0) {
        _sync.unlock ();
        _sync.lock ();
    } else {
        const int rc = _cond_var.wait (&_sync, timeout_);
        if (rc == -1) {
            errno_assert (errno == EAGAIN || errno == EINTR);
            _sync.unlock ();
            return -1;
        }
    }

    const bool ok = _cpipe.read (cmd_);
    _sync.unlock ();

    if (!ok) {
        errno = EAGAIN;
        return -1;
    }

    return 0;
}

bool zmq::mailbox_t::valid () const
{
    return true;
}

void zmq::mailbox_t::set_io_context (boost::asio::io_context *io_context_,
                                     mailbox_handler_t handler_,
                                     void *handler_arg_,
                                     mailbox_pre_post_t pre_post_)
{
    _io_context = io_context_;
    _handler = handler_;
    _handler_arg = handler_arg_;
    _pre_post = pre_post_;
}

void zmq::mailbox_t::schedule_if_needed ()
{
    if (!_io_context || !_handler)
        return;

    _sync.lock ();
    const bool has_data = _cpipe.check_read ();
    _sync.unlock ();

    if (!has_data)
        return;

    if (!_scheduled.exchange (true, std::memory_order_acquire)) {
        if (_pre_post)
            _pre_post (_handler_arg);
        boost::asio::post (*_io_context, [this]() {
            if (_handler)
                _handler (_handler_arg);
        });
    }
}

bool zmq::mailbox_t::reschedule_if_needed ()
{
    _scheduled.store (false, std::memory_order_release);
    _sync.lock ();
    const bool has_data = _cpipe.check_read ();
    _sync.unlock ();

    if (!has_data)
        return false;
    if (_scheduled.exchange (true, std::memory_order_acquire))
        return false;
    return true;
}
