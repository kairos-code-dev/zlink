/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "core/mailbox.hpp"
#include "utils/err.hpp"
#include "core/signaler.hpp"

#include <boost/asio.hpp>
#include <algorithm>

zmq::mailbox_t::mailbox_t ()
{
    const bool ok = _cpipe.check_read ();
    zmq_assert (!ok);
    _active = false;
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

zmq::fd_t zmq::mailbox_t::get_fd () const
{
    return _signaler.get_fd ();
}

void zmq::mailbox_t::send (const command_t &cmd_)
{
    _sync.lock ();
    _cpipe.write (cmd_, false);
    const bool ok = _cpipe.flush ();
    if (!ok) {
        // Signal all registered signalers for ZMQ_FD support
        for (std::vector<signaler_t *>::iterator it = _signalers.begin (),
                                                 end = _signalers.end ();
             it != end; ++it) {
            (*it)->send ();
        }
    }
    _sync.unlock ();

    if (!ok) {
        _signaler.send ();
        schedule_if_needed ();
    }
}

int zmq::mailbox_t::recv (command_t *cmd_, int timeout_)
{
    if (!_active) {
        if (timeout_ == 0) {
            // Avoid poll syscall on non-blocking checks.
            const int rc = _signaler.recv_failable ();
            if (rc == -1) {
                errno_assert (errno == EAGAIN);
                return -1;
            }
        } else {
            const int rc = _signaler.wait (timeout_);
            if (rc == -1) {
                errno_assert (errno == EAGAIN || errno == EINTR);
                return -1;
            }
            _signaler.recv ();
        }
        _active = true;
    }

    if (_cpipe.read (cmd_))
        return 0;

    _active = false;
    errno = EAGAIN;
    return -1;
}

bool zmq::mailbox_t::valid () const
{
    return _signaler.valid ();
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
    const bool has_data = _cpipe.check_read ();

    if (!has_data)
        return false;
    if (_scheduled.exchange (true, std::memory_order_acquire))
        return false;
    return true;
}

void zmq::mailbox_t::add_signaler (signaler_t *signaler_)
{
    _signalers.push_back (signaler_);
}

void zmq::mailbox_t::remove_signaler (signaler_t *signaler_)
{
    const std::vector<signaler_t *>::iterator end = _signalers.end ();
    const std::vector<signaler_t *>::iterator it =
      std::find (_signalers.begin (), end, signaler_);
    if (it != end)
        _signalers.erase (it);
}

void zmq::mailbox_t::clear_signalers ()
{
    _signalers.clear ();
}
