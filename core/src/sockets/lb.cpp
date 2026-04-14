/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "sockets/lb.hpp"
#include "core/pipe.hpp"
#include "utils/err.hpp"
#include "core/msg.hpp"

zlink::lb_t::lb_t () : _active (0), _current (0), _more (false), _dropping (false)
{
}

zlink::lb_t::~lb_t ()
{
    zlink_assert (_pipes.empty ());
}

void zlink::lb_t::attach (pipe_t *pipe_)
{
    _pipes.push_back (pipe_);
    _admitted[pipe_] = true;
    activated (pipe_);
}

void zlink::lb_t::pipe_terminated (pipe_t *pipe_)
{
    const pipes_t::size_type index = _pipes.index (pipe_);

    //  If we are in the middle of multipart message and current pipe
    //  have disconnected, we have to drop the remainder of the message.
    if (index == _current && _more)
        _dropping = true;

    //  Remove the pipe from the list; adjust number of active pipes
    //  accordingly.
    if (index < _active) {
        _active--;
        _pipes.swap (index, _active);
        if (_current == _active)
            _current = 0;
    }
    _pipes.erase (pipe_);
    _admitted.erase (pipe_);
}

void zlink::lb_t::activated (pipe_t *pipe_)
{
    const std::map<pipe_t *, bool>::const_iterator admitted_it =
      _admitted.find (pipe_);
    if (admitted_it != _admitted.end () && !admitted_it->second)
        return;

    const pipes_t::size_type index = _pipes.index (pipe_);
    if (index < _active)
        return;

    //  Move the pipe to the list of active pipes.
    _pipes.swap (index, _active);
    _active++;
}

void zlink::lb_t::set_admitted (pipe_t *pipe_, bool admitted_)
{
    if (!pipe_)
        return;

    std::map<pipe_t *, bool>::iterator it = _admitted.find (pipe_);
    if (it == _admitted.end ())
        return;
    if (it->second == admitted_)
        return;

    it->second = admitted_;

    const pipes_t::size_type index = _pipes.index (pipe_);
    if (!admitted_) {
        if (index == _current && _more)
            _dropping = true;

        if (index < _active) {
            _active--;
            _pipes.swap (index, _active);
            if (_current == _active)
                _current = 0;
            else if (_current > index && _current <= _active)
                --_current;
        }
        return;
    }

    if (index >= _active && pipe_->check_write ()) {
        _pipes.swap (index, _active);
        _active++;
    }
}

bool zlink::lb_t::admitted (pipe_t *pipe_) const
{
    std::map<pipe_t *, bool>::const_iterator it = _admitted.find (pipe_);
    return it != _admitted.end () && it->second;
}

bool zlink::lb_t::has_admitted_pipe () const
{
    for (std::map<pipe_t *, bool>::const_iterator it = _admitted.begin ();
         it != _admitted.end (); ++it) {
        if (it->second)
            return true;
    }
    return false;
}

int zlink::lb_t::send (msg_t *msg_)
{
    return sendpipe (msg_, NULL);
}

int zlink::lb_t::sendpipe (msg_t *msg_, pipe_t **pipe_)
{
    //  Drop the message if required. If we are at the end of the message
    //  switch back to non-dropping mode.
    if (_dropping) {
        _more = (msg_->flags () & msg_t::more) != 0;
        _dropping = _more;

        int rc = msg_->close ();
        errno_assert (rc == 0);
        rc = msg_->init ();
        errno_assert (rc == 0);
        return 0;
    }

    // Hot path: DEALER_DEALER single benchmarks run with one active pipe.
    // Keep the one-pipe steady-state out of the general load-balancing loop.
    if (_active == 1 && _current == 0) {
        pipe_t *pipe = _pipes[0];
        const bool more = (msg_->flags () & msg_t::more) != 0;
        const bool ok = more ? pipe->write (msg_) : pipe->write_and_flush (msg_);
        if (!ok) {
            _active = 0;
            errno = EAGAIN;
            return -1;
        }

        if (pipe_)
            *pipe_ = pipe;

        _more = more;

        const int rc = msg_->init ();
        errno_assert (rc == 0);
        return 0;
    }

    while (_active > 0) {
        const bool more = (msg_->flags () & msg_t::more) != 0;
        const bool ok = more ? _pipes[_current]->write (msg_)
                             : _pipes[_current]->write_and_flush (msg_);
        if (ok) {
            if (pipe_)
                *pipe_ = _pipes[_current];
            break;
        }

        // If send fails for multi-part msg rollback other
        // parts sent earlier and return EAGAIN.
        // Application should handle this as suitable
        if (_more) {
            _pipes[_current]->rollback ();
            // At this point the pipe is already being deallocated
            // and the first N frames are unreachable (_outpipe is
            // most likely already NULL so rollback won't actually do
            // anything and they can't be un-written to deliver later).
            // Return EFAULT to socket_base caller to drop current message
            // and any other subsequent frames to avoid them being
            // "stuck" and received when a new client reconnects, which
            // would break atomicity of multi-part messages (in blocking mode
            // socket_base just tries again and again to send the same message)
            // Note that given dropping mode returns 0, the user will
            // never know that the message could not be delivered, but
            // can't really fix it without breaking backward compatibility.
            // -2/EAGAIN will make sure socket_base caller does not re-enter
            // immediately or after a short sleep in blocking mode.
            _dropping = (msg_->flags () & msg_t::more) != 0;
            _more = false;
            errno = EAGAIN;
            return -2;
        }

        _active--;
        if (_current < _active)
            _pipes.swap (_current, _active);
        else
            _current = 0;
    }

    //  If there are no pipes we cannot send the message.
    if (_active == 0) {
        errno = has_admitted_pipe () ? EAGAIN : ECONNREFUSED;
        return -1;
    }

    //  If it's final part of the message we can flush it downstream and
    //  continue round-robining (load balance).
    _more = (msg_->flags () & msg_t::more) != 0;
    if (!_more) {
        if (++_current >= _active)
            _current = 0;
    }

    //  Detach the message from the data buffer.
    const int rc = msg_->init ();
    errno_assert (rc == 0);

    return 0;
}

bool zlink::lb_t::has_out ()
{
    //  If one part of the message was already written we can definitely
    //  write the rest of the message.
    if (_more)
        return true;

    if (_active == 1 && _current == 0) {
        if (_pipes[0]->check_write ())
            return true;

        _active = 0;
        return false;
    }

    while (_active > 0) {
        //  Check whether a pipe has room for another message.
        if (_pipes[_current]->check_write ())
            return true;

        //  Deactivate the pipe.
        _active--;
        _pipes.swap (_current, _active);
        if (_current == _active)
            _current = 0;
    }

    return false;
}
