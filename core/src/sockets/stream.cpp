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

zlink::stream_t::stream_t (class ctx_t *parent_, uint32_t tid_, int sid_) :
    routing_socket_base_t (parent_, tid_, sid_),
    _prefetched (false),
    _routing_id_sent (false),
    _prefetched_routing_id_value (0),
    _current_out (NULL),
    _more_out (false),
    _next_integral_routing_id (1)
{
    options.type = ZLINK_STREAM;
    options.backlog = 65536;
    const int stream_batch_size = 65536;
    if (options.in_batch_size < stream_batch_size)
        options.in_batch_size = stream_batch_size;
    if (options.out_batch_size < stream_batch_size)
        options.out_batch_size = stream_batch_size;

    _prefetched_msg.init ();
}

zlink::stream_t::~stream_t ()
{
    _prefetched_msg.close ();
}

void zlink::stream_t::xattach_pipe (pipe_t *pipe_,
                                    bool subscribe_to_all_,
                                    bool locally_initiated_)
{
    LIBZLINK_UNUSED (subscribe_to_all_);

    zlink_assert (pipe_);

    identify_peer (pipe_, locally_initiated_);
    _fq.attach (pipe_);

    queue_event (pipe_->get_server_socket_routing_id (), stream_event_connect);
}

void zlink::stream_t::xpipe_terminated (pipe_t *pipe_)
{
    const uint32_t server_routing_id = pipe_->get_server_socket_routing_id ();

    erase_out_pipe (pipe_);
    _fq.pipe_terminated (pipe_);
    if (pipe_ == _current_out)
        _current_out = NULL;
    if (server_routing_id != 0)
        _out_by_id.erase (server_routing_id);

    queue_event (server_routing_id, stream_event_disconnect);
}

void zlink::stream_t::xread_activated (pipe_t *pipe_)
{
    _fq.activated (pipe_);
}

int zlink::stream_t::xsend (msg_t *msg_)
{
    if (!_more_out) {
        zlink_assert (!_current_out);

        if (msg_->flags () & msg_t::more) {
            if (msg_->size () != 4) {
                errno = EINVAL;
                return -1;
            }

            const uint32_t routing_id =
              get_uint32 (static_cast<unsigned char *> (msg_->data ()));
            const std::map<uint32_t, pipe_t *>::iterator it =
              _out_by_id.find (routing_id);

            if (it != _out_by_id.end ()) {
                _current_out = it->second;
                if (!_current_out->check_write ()) {
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

        const uint32_t routing_id = _current_out->get_server_socket_routing_id ();
        if (unlikely (routing_id == 0 || msg_->set_routing_id (routing_id) != 0)) {
            _current_out = NULL;
            return -1;
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

int zlink::stream_t::xrecv (msg_t *msg_)
{
    if (_prefetched) {
        if (!_routing_id_sent) {
            int rc = msg_->close ();
            errno_assert (rc == 0);
            rc = msg_->init_size (4);
            errno_assert (rc == 0);
            put_uint32 (static_cast<unsigned char *> (msg_->data ()),
                        _prefetched_routing_id_value);
            metadata_t *metadata = _prefetched_msg.metadata ();
            if (metadata)
                msg_->set_metadata (metadata);
            msg_->set_flags (msg_t::more);
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

    zlink_assert (pipe != NULL);

    _prefetched_routing_id_value = _prefetched_msg.get_routing_id ();
    if (_prefetched_routing_id_value == 0)
        _prefetched_routing_id_value = pipe->get_server_socket_routing_id ();

    rc = msg_->close ();
    errno_assert (rc == 0);
    rc = msg_->init_size (4);
    errno_assert (rc == 0);

    metadata_t *metadata = _prefetched_msg.metadata ();
    if (metadata)
        msg_->set_metadata (metadata);

    put_uint32 (static_cast<unsigned char *> (msg_->data ()),
                _prefetched_routing_id_value);
    msg_->set_flags (msg_t::more);

    _prefetched = true;
    _routing_id_sent = true;

    return 0;
}

bool zlink::stream_t::xhas_in ()
{
    if (_prefetched)
        return true;

    if (prefetch_event ())
        return true;

    pipe_t *pipe = NULL;
    int rc = _fq.recvpipe (&_prefetched_msg, &pipe);
    if (rc != 0)
        return false;

    zlink_assert (pipe != NULL);

    _prefetched_routing_id_value = _prefetched_msg.get_routing_id ();
    if (_prefetched_routing_id_value == 0)
        _prefetched_routing_id_value = pipe->get_server_socket_routing_id ();

    _prefetched = true;
    _routing_id_sent = false;

    return true;
}

bool zlink::stream_t::xhas_out ()
{
    return true;
}

int zlink::stream_t::xsetsockopt (int option_,
                                  const void *optval_,
                                  size_t optvallen_)
{
    if (option_ == ZLINK_CONNECT_ROUTING_ID) {
        if (optval_ && optvallen_ == 4) {
            return routing_socket_base_t::xsetsockopt (option_, optval_,
                                                       optvallen_);
        }
        errno = EINVAL;
        return -1;
    }

    return routing_socket_base_t::xsetsockopt (option_, optval_, optvallen_);
}

void zlink::stream_t::identify_peer (pipe_t *pipe_, bool locally_initiated_)
{
    blob_t routing_id;

    if (locally_initiated_ && connect_routing_id_is_set ()) {
        const std::string connect_routing_id = extract_connect_routing_id ();
        if (connect_routing_id.size () == 4) {
            routing_id.set (
              reinterpret_cast<const unsigned char *> (
                connect_routing_id.c_str ()),
              connect_routing_id.size ());
            zlink_assert (!has_out_pipe (routing_id));
        }
    }

    if (routing_id.size () == 0) {
        unsigned char buf[4];
        put_uint32 (buf, _next_integral_routing_id++);
        if (_next_integral_routing_id == 0)
            _next_integral_routing_id = 1;
        routing_id.set (buf, sizeof buf);
    }

    pipe_->set_router_socket_routing_id (routing_id);
    pipe_->set_server_socket_routing_id (get_uint32 (routing_id.data ()));
    add_out_pipe (ZLINK_MOVE (routing_id), pipe_);
    _out_by_id[pipe_->get_server_socket_routing_id ()] = pipe_;
}

void zlink::stream_t::queue_event (uint32_t routing_id_value_,
                                   unsigned char code_)
{
    stream_event_t ev;
    ev.routing_id_value = routing_id_value_;
    ev.code = code_;
    _pending_events.push_back (ZLINK_MOVE (ev));
}

bool zlink::stream_t::prefetch_event ()
{
    if (_pending_events.empty ())
        return false;

    stream_event_t ev = ZLINK_MOVE (_pending_events.front ());
    _pending_events.pop_front ();

    _prefetched_routing_id_value = ev.routing_id_value;

    int rc = _prefetched_msg.init_size (1);
    errno_assert (rc == 0);
    *static_cast<unsigned char *> (_prefetched_msg.data ()) = ev.code;

    _prefetched = true;
    _routing_id_sent = false;

    return true;
}
