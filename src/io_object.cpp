/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"
#include "io_object.hpp"
#include "io_thread.hpp"
#include "err.hpp"

zmq::io_object_t::io_object_t (io_thread_t *io_thread_) : _poller (NULL)
{
    if (io_thread_)
        plug (io_thread_);
}

zmq::io_object_t::~io_object_t ()
{
}

void zmq::io_object_t::plug (io_thread_t *io_thread_)
{
    zmq_assert (io_thread_);
    zmq_assert (!_poller);

    //  Retrieve the poller from the thread we are running in.
    _poller = io_thread_->get_poller ();
}

void zmq::io_object_t::unplug ()
{
    zmq_assert (_poller);

    //  Forget about old poller in preparation to be migrated
    //  to a different I/O thread.
    _poller = NULL;
}

void zmq::io_object_t::add_timer (int timeout_, int id_)
{
    _poller->add_timer (timeout_, this, id_);
}

void zmq::io_object_t::cancel_timer (int id_)
{
    _poller->cancel_timer (this, id_);
}

void zmq::io_object_t::in_event ()
{
    zmq_assert (false);
}

void zmq::io_object_t::out_event ()
{
    zmq_assert (false);
}

void zmq::io_object_t::timer_event (int)
{
    zmq_assert (false);
}
