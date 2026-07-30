/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "sockets/internal/lb.hpp"
#include "core/pipe.hpp"
#include "utils/err.hpp"
#include "core/msg.hpp"

namespace
{
uint32_t gcd_u32 (uint32_t lhs_, uint32_t rhs_)
{
    while (rhs_ != 0) {
        const uint32_t rem = lhs_ % rhs_;
        lhs_ = rhs_;
        rhs_ = rem;
    }
    return lhs_;
}
}

zlink::lb_t::lb_t () :
    _active (0),
    _current (0),
    _more (false),
    _dropping (false),
    _weighted_dirty (true),
    _weighted_enabled (false),
    _weighted_current (0),
    _weighted_multipart_pipe (NULL)
{
}

zlink::lb_t::~lb_t ()
{
    zlink_assert (_pipes.empty ());
}

void zlink::lb_t::attach (pipe_t *pipe_)
{
    _pipes.push_back (pipe_);
    _weights[pipe_] = 100;
    mark_weighted_dirty ();
    activated (pipe_);
}

void zlink::lb_t::pipe_terminated (pipe_t *pipe_)
{
    const pipes_t::size_type index = _pipes.index (pipe_);

    //  If we are in the middle of multipart message and current pipe
    //  have disconnected, we have to drop the remainder of the message.
    if (index == _current && _more)
        _dropping = true;
    if (pipe_ == _weighted_multipart_pipe && _more)
        _dropping = true;

    //  Remove the pipe from the list; adjust number of active pipes
    //  accordingly.
    if (index < _active) {
        _active--;
        _pipes.swap (index, _active);
        if (_current == _active)
            _current = 0;
        mark_weighted_dirty ();
    }
    _pipes.erase (pipe_);
    _weights.erase (pipe_);
    mark_weighted_dirty ();
}

void zlink::lb_t::activated (pipe_t *pipe_)
{
    const std::map<pipe_t *, uint32_t>::const_iterator weight_it = _weights.find (pipe_);
    if (weight_it != _weights.end () && weight_it->second == 0)
        return;

    const pipes_t::size_type index = _pipes.index (pipe_);
    if (index < _active)
        return;

    //  Move the pipe to the list of active pipes.
    _pipes.swap (index, _active);
    _active++;
    mark_weighted_dirty ();
}

void zlink::lb_t::set_weight (pipe_t *pipe_, uint32_t weight_)
{
    if (!pipe_)
        return;

    if (weight_ > 100)
        weight_ = 100;

    std::map<pipe_t *, uint32_t>::iterator it = _weights.find (pipe_);
    if (it == _weights.end ())
        return;
    if (it->second == weight_)
        return;

    it->second = weight_;
    mark_weighted_dirty ();

    const pipes_t::size_type index = _pipes.index (pipe_);
    if (weight_ == 0) {
        if (index == _current && _more)
            _dropping = true;
        if (pipe_ == _weighted_multipart_pipe && _more)
            _dropping = true;

        if (index < _active) {
            _active--;
            _pipes.swap (index, _active);
            if (_current == _active)
                _current = 0;
            else if (_current > index && _current <= _active)
                --_current;
            mark_weighted_dirty ();
        }
        return;
    }

    if (index >= _active && pipe_->check_write ()) {
        _pipes.swap (index, _active);
        _active++;
        mark_weighted_dirty ();
    }
}

uint32_t zlink::lb_t::weight (pipe_t *pipe_) const
{
    std::map<pipe_t *, uint32_t>::const_iterator it = _weights.find (pipe_);
    return it != _weights.end () ? it->second : 0;
}

bool zlink::lb_t::has_positive_weight_pipe () const
{
    for (std::map<pipe_t *, uint32_t>::const_iterator it = _weights.begin (); it != _weights.end ();
         ++it) {
        if (it->second > 0)
            return true;
    }
    return false;
}

bool zlink::lb_t::contains (pipe_t *pipe_) const
{
    if (!pipe_)
        return false;

    for (pipes_t::size_type i = 0; i < _pipes.size (); ++i) {
        if (_pipes[i] == pipe_)
            return true;
    }
    return false;
}

void zlink::lb_t::deactivate (pipe_t *pipe_)
{
    const pipes_t::size_type index = _pipes.index (pipe_);
    if (index >= _active)
        return;

    _active--;
    _pipes.swap (index, _active);
    if (_current == _active)
        _current = 0;
    else if (_current > index && _current <= _active)
        --_current;
    mark_weighted_dirty ();
}

void zlink::lb_t::mark_weighted_dirty ()
{
    _weighted_dirty = true;
}

void zlink::lb_t::rebuild_weighted_schedule ()
{
    if (!_weighted_dirty)
        return;

    _weighted_schedule.clear ();
    _weighted_enabled = false;

    uint32_t first_weight = 0;
    uint32_t weight_gcd = 0;
    bool have_first = false;
    bool all_equal = true;
    for (pipes_t::size_type i = 0; i < _active; ++i) {
        const uint32_t pipe_weight = weight (_pipes[i]);
        if (pipe_weight > 0)
            weight_gcd = weight_gcd == 0 ? pipe_weight : gcd_u32 (weight_gcd, pipe_weight);
        if (!have_first) {
            first_weight = pipe_weight;
            have_first = true;
        } else if (pipe_weight != first_weight) {
            all_equal = false;
        }
    }

    if (_active > 1 && !all_equal) {
        for (pipes_t::size_type i = 0; i < _active; ++i) {
            const uint32_t pipe_weight = weight (_pipes[i]);
            const uint32_t slots = weight_gcd > 0 ? pipe_weight / weight_gcd : pipe_weight;
            for (uint32_t n = 0; n < slots; ++n)
                _weighted_schedule.push_back (_pipes[i]);
        }
        _weighted_enabled = !_weighted_schedule.empty ();
        if (_weighted_current >= _weighted_schedule.size ())
            _weighted_current = 0;
    } else {
        _weighted_current = 0;
    }

    _weighted_dirty = false;
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

    if (_more && _weighted_multipart_pipe) {
        const bool more = (msg_->flags () & msg_t::more) != 0;
        const bool ok = more ? _weighted_multipart_pipe->write (msg_)
                             : _weighted_multipart_pipe->write_and_flush (msg_);
        if (!ok) {
            _weighted_multipart_pipe->rollback ();
            _weighted_multipart_pipe = NULL;
            _dropping = more;
            _more = false;
            errno = EAGAIN;
            return -2;
        }
        if (pipe_)
            *pipe_ = _weighted_multipart_pipe;
        _more = more;
        if (!_more)
            _weighted_multipart_pipe = NULL;
        const int rc = msg_->init ();
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
            if (_more) {
                pipe->rollback ();
                _weighted_multipart_pipe = NULL;
                _dropping = more;
                _more = false;
                errno = EAGAIN;
                return -2;
            }
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

    rebuild_weighted_schedule ();

    if (!_more && _weighted_enabled) {
        while (_active > 0 && !_weighted_schedule.empty ()) {
            pipe_t *pipe = _weighted_schedule[_weighted_current];
            const bool more = (msg_->flags () & msg_t::more) != 0;
            const bool ok = more ? pipe->write (msg_) : pipe->write_and_flush (msg_);
            if (ok) {
                if (pipe_)
                    *pipe_ = pipe;
                _more = more;
                _weighted_multipart_pipe = more ? pipe : NULL;
                if (++_weighted_current >= _weighted_schedule.size ())
                    _weighted_current = 0;
                const int rc = msg_->init ();
                errno_assert (rc == 0);
                return 0;
            }

            // A failed write changes current writability, not the peer's
            // advertised routing policy. Preserve the configured weight so
            // write activation can restore this pipe.
            deactivate (pipe);
            rebuild_weighted_schedule ();
        }

        errno = has_positive_weight_pipe () ? EAGAIN : ECONNREFUSED;
        return -1;
    }

    while (_active > 0) {
        const bool more = (msg_->flags () & msg_t::more) != 0;
        const bool ok =
          more ? _pipes[_current]->write (msg_) : _pipes[_current]->write_and_flush (msg_);
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
            // At this point the pipe is already being deallocated and the
            // frames written earlier cannot be recovered. Enter dropping mode
            // for the remaining frames so the next logical message starts with
            // a clean multipart boundary. -2/EAGAIN tells socket_base not to
            // retry this frame immediately in blocking mode.
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
        errno = has_positive_weight_pipe () ? EAGAIN : ECONNREFUSED;
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

void zlink::lb_t::rollback ()
{
    if (_weighted_multipart_pipe)
        _weighted_multipart_pipe->rollback ();
    else if (_more && _current < _pipes.size ())
        _pipes[_current]->rollback ();

    _more = false;
    _dropping = false;
    _weighted_multipart_pipe = NULL;
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
        mark_weighted_dirty ();
        return false;
    }

    while (_active > 0) {
        //  Check whether a pipe has room for another message.
        if (_pipes[_current]->check_write ())
            return true;

        //  Deactivate the pipe.
        _active--;
        _pipes.swap (_current, _active);
        mark_weighted_dirty ();
        if (_current == _active)
            _current = 0;
    }

    return false;
}
