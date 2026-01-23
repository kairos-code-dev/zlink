/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "sockets/stream.hpp"
#include "core/pipe.hpp"
#include "protocol/wire.hpp"
#include "utils/err.hpp"
#include "utils/likely.hpp"

namespace
{
const unsigned char stream_event_connect = 0x01;
const unsigned char stream_event_disconnect = 0x00;
}

zmq::stream_t::stream_t (class ctx_t *parent_, uint32_t tid_, int sid_) :
    routing_socket_base_t (parent_, tid_, sid_),
    _prefetched (false),
    _routing_id_sent (false),
    _current_out (NULL),
    _more_out (false),
    _next_integral_routing_id (1)
{
    options.type = ZMQ_STREAM;
    const int stream_batch_size = 65536;
    if (options.in_batch_size < stream_batch_size)
        options.in_batch_size = stream_batch_size;
    if (options.out_batch_size < stream_batch_size)
        options.out_batch_size = stream_batch_size;

    _prefetched_routing_id.init ();
    _prefetched_msg.init ();
}

zmq::stream_t::~stream_t ()
{
    _prefetched_routing_id.close ();
    _prefetched_msg.close ();
}

void zmq::stream_t::xattach_pipe (pipe_t *pipe_,
                                  bool subscribe_to_all_,
                                  bool locally_initiated_)
{
    LIBZMQ_UNUSED (subscribe_to_all_);

    zmq_assert (pipe_);

    identify_peer (pipe_, locally_initiated_);
    _fq.attach (pipe_);

    queue_event (pipe_->get_routing_id (), stream_event_connect);
}

void zmq::stream_t::xpipe_terminated (pipe_t *pipe_)
{
    const blob_t &routing_id = pipe_->get_routing_id ();

    erase_out_pipe (pipe_);
    _fq.pipe_terminated (pipe_);
    if (pipe_ == _current_out)
        _current_out = NULL;

    queue_event (routing_id, stream_event_disconnect);
}

void zmq::stream_t::xread_activated (pipe_t *pipe_)
{
    _fq.activated (pipe_);
}

int zmq::stream_t::xsend (msg_t *msg_)
{
    if (!_more_out) {
        zmq_assert (!_current_out);

        if (msg_->flags () & msg_t::more) {
            if (msg_->size () != 4) {
                errno = EINVAL;
                return -1;
            }

            out_pipe_t *out_pipe = lookup_out_pipe (
              blob_t (static_cast<unsigned char *> (msg_->data ()),
                      msg_->size (), reference_tag_t ()));

            if (out_pipe) {
                _current_out = out_pipe->pipe;
                if (!_current_out->check_write ()) {
                    out_pipe->active = false;
                    _current_out = NULL;
                    errno = EAGAIN;
                    return -1;
                }
            } else {
                errno = EHOSTUNREACH;
                return -1;
            }

            _more_out = true;
        }

        int rc = msg_->close ();
        errno_assert (rc == 0);
        rc = msg_->init ();
        errno_assert (rc == 0);
        return 0;
    }

    msg_->reset_flags (msg_t::more);
    _more_out = false;

    if (_current_out) {
        if (msg_->size () == 1) {
            const unsigned char *data =
              static_cast<unsigned char *> (msg_->data ());
            if (data[0] == stream_event_disconnect) {
                _current_out->terminate (false);
                int rc = msg_->close ();
                errno_assert (rc == 0);
                rc = msg_->init ();
                errno_assert (rc == 0);
                _current_out = NULL;
                return 0;
            }
        }

        const bool ok = _current_out->write (msg_);
        if (likely (ok))
            _current_out->flush ();
        _current_out = NULL;
    } else {
        const int rc = msg_->close ();
        errno_assert (rc == 0);
    }

    const int rc = msg_->init ();
    errno_assert (rc == 0);

    return 0;
}

int zmq::stream_t::xrecv (msg_t *msg_)
{
    if (_prefetched) {
        if (!_routing_id_sent) {
            const int rc = msg_->move (_prefetched_routing_id);
            errno_assert (rc == 0);
            _routing_id_sent = true;
        } else {
            const int rc = msg_->move (_prefetched_msg);
            errno_assert (rc == 0);
            _prefetched = false;
        }
        return 0;
    }

    if (prefetch_event ()) {
        return xrecv (msg_);
    }

    pipe_t *pipe = NULL;
    int rc = _fq.recvpipe (&_prefetched_msg, &pipe);
    if (rc != 0)
        return -1;

    zmq_assert (pipe != NULL);

    const blob_t &routing_id = pipe->get_routing_id ();
    rc = msg_->close ();
    errno_assert (rc == 0);
    rc = msg_->init_size (routing_id.size ());
    errno_assert (rc == 0);

    metadata_t *metadata = _prefetched_msg.metadata ();
    if (metadata)
        msg_->set_metadata (metadata);

    memcpy (msg_->data (), routing_id.data (), routing_id.size ());
    msg_->set_flags (msg_t::more);

    _prefetched = true;
    _routing_id_sent = true;

    return 0;
}

bool zmq::stream_t::xhas_in ()
{
    if (_prefetched)
        return true;

    if (prefetch_event ())
        return true;

    pipe_t *pipe = NULL;
    int rc = _fq.recvpipe (&_prefetched_msg, &pipe);
    if (rc != 0)
        return false;

    zmq_assert (pipe != NULL);

    const blob_t &routing_id = pipe->get_routing_id ();
    rc = _prefetched_routing_id.init_size (routing_id.size ());
    errno_assert (rc == 0);

    metadata_t *metadata = _prefetched_msg.metadata ();
    if (metadata)
        _prefetched_routing_id.set_metadata (metadata);

    memcpy (_prefetched_routing_id.data (), routing_id.data (),
            routing_id.size ());
    _prefetched_routing_id.set_flags (msg_t::more);

    _prefetched = true;
    _routing_id_sent = false;

    return true;
}

bool zmq::stream_t::xhas_out ()
{
    return true;
}

int zmq::stream_t::xsetsockopt (int option_,
                                const void *optval_,
                                size_t optvallen_)
{
    if (option_ == ZMQ_CONNECT_ROUTING_ID) {
        if (optval_ && optvallen_ == 4) {
            return routing_socket_base_t::xsetsockopt (option_, optval_,
                                                       optvallen_);
        }
        errno = EINVAL;
        return -1;
    }

    return routing_socket_base_t::xsetsockopt (option_, optval_, optvallen_);
}

void zmq::stream_t::identify_peer (pipe_t *pipe_, bool locally_initiated_)
{
    blob_t routing_id;

    if (locally_initiated_ && connect_routing_id_is_set ()) {
        const std::string connect_routing_id = extract_connect_routing_id ();
        if (connect_routing_id.size () == 4) {
            routing_id.set (
              reinterpret_cast<const unsigned char *> (
                connect_routing_id.c_str ()),
              connect_routing_id.size ());
            zmq_assert (!has_out_pipe (routing_id));
        }
    }

    if (routing_id.size () == 0) {
        unsigned char buf[4];
        put_uint32 (buf, _next_integral_routing_id++);
        if (_next_integral_routing_id == 0)
            _next_integral_routing_id = 1;
        routing_id.set (buf, sizeof buf);
        memcpy (options.routing_id, routing_id.data (), routing_id.size ());
        options.routing_id_size =
          static_cast<unsigned char> (routing_id.size ());
    }

    pipe_->set_router_socket_routing_id (routing_id);
    add_out_pipe (ZMQ_MOVE (routing_id), pipe_);
}

void zmq::stream_t::queue_event (const blob_t &routing_id_,
                                 unsigned char code_)
{
    stream_event_t ev;
    ev.routing_id.set (routing_id_.data (), routing_id_.size ());
    ev.code = code_;
    _pending_events.push_back (ZMQ_MOVE (ev));
}

bool zmq::stream_t::prefetch_event ()
{
    if (_pending_events.empty ())
        return false;

    stream_event_t ev = ZMQ_MOVE (_pending_events.front ());
    _pending_events.pop_front ();

    int rc = _prefetched_routing_id.init_size (ev.routing_id.size ());
    errno_assert (rc == 0);
    memcpy (_prefetched_routing_id.data (), ev.routing_id.data (),
            ev.routing_id.size ());
    _prefetched_routing_id.set_flags (msg_t::more);

    rc = _prefetched_msg.init_size (1);
    errno_assert (rc == 0);
    *static_cast<unsigned char *> (_prefetched_msg.data ()) = ev.code;

    _prefetched = true;
    _routing_id_sent = false;

    return true;
}
