/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "utils/macros.hpp"
#include "sockets/router.hpp"
#include "sockets/socket_dispatch_loop_internal.hpp"
#include "core/pipe.hpp"
#include "protocol/wire.hpp"
#include "utils/random.hpp"
#include "utils/likely.hpp"
#include "utils/err.hpp"
#include <cstdlib>
#include <cstdio>

namespace
{
void format_routing_id_debug (const zlink_routing_id_t *rid_,
                              char *buf_,
                              size_t buf_size_)
{
    if (!buf_ || buf_size_ == 0) {
        return;
    }

    if (!rid_ || rid_->size == 0) {
        std::snprintf (buf_, buf_size_, "<empty>");
        return;
    }

    size_t used = 0;
    for (size_t i = 0; i < rid_->size && used + 4 < buf_size_; ++i) {
        const unsigned char c = rid_->data[i];
        const int rc = std::snprintf (buf_ + used,
                                      buf_size_ - used,
                                      "%c%02X",
                                      (c >= 32 && c <= 126)
                                        ? static_cast<char> (c)
                                        : '.',
                                      static_cast<unsigned> (c));
        if (rc <= 0)
            break;
        used += static_cast<size_t> (rc);
        if (i + 1 < rid_->size && used + 2 < buf_size_) {
            buf_[used++] = ' ';
            buf_[used] = '\0';
        }
    }
}

void format_blob_routing_id_debug (const zlink::blob_t &routing_id_,
                                   char *buf_,
                                   size_t buf_size_)
{
    zlink_routing_id_t rid;
    rid.size = static_cast<uint8_t> (
      routing_id_.size () < sizeof (rid.data) ? routing_id_.size ()
                                              : sizeof (rid.data));
    if (rid.size > 0)
        memcpy (rid.data, routing_id_.data (), rid.size);
    format_routing_id_debug (&rid, buf_, buf_size_);
}

void clear_dispatch_source_rid (zlink_routing_id_t *rid_, bool *valid_)
{
    if (!rid_ || !valid_)
        return;
    rid_->size = 0;
    *valid_ = false;
}

void store_dispatch_source_rid (zlink_routing_id_t *rid_,
                                bool *valid_,
                                zlink::msg_t *msg_)
{
    if (!rid_ || !valid_ || !msg_)
        return;

    const size_t size = msg_->size ();
    zlink_assert (size <= sizeof (rid_->data));
    rid_->size = static_cast<uint8_t> (size);
    if (size > 0)
        memcpy (rid_->data, msg_->data (), size);
    *valid_ = true;
}

void copy_source_rid_from_blob (const zlink::blob_t &routing_id_,
                                zlink_routing_id_t *out_)
{
    if (!out_)
        return;

    const size_t size = routing_id_.size () < sizeof (out_->data)
                          ? routing_id_.size ()
                          : sizeof (out_->data);
    out_->size = static_cast<uint8_t> (size);
    if (size > 0)
        memcpy (out_->data, routing_id_.data (), size);
}

void copy_source_rid_from_msg (const zlink::msg_t &msg_,
                               zlink_routing_id_t *out_)
{
    if (!out_)
        return;

    const size_t size = msg_.size () < sizeof (out_->data) ? msg_.size ()
                                                            : sizeof (out_->data);
    out_->size = static_cast<uint8_t> (size);
    if (size > 0)
        memcpy (out_->data, const_cast<zlink::msg_t &> (msg_).data (), size);
}

void copy_router_pipe_source_rid (zlink::pipe_t *pipe_,
                                  zlink_routing_id_t *out_)
{
    if (!out_) {
        return;
    }

    out_->size = 0;
    if (!pipe_)
        return;

    const zlink::blob_t &routing_id = pipe_->get_routing_id ();
    if (routing_id.size () > 0) {
        copy_source_rid_from_blob (routing_id, out_);
        return;
    }

    zlink::pipe_t *peer = pipe_->get_peer ();
    if (!peer)
        return;

    copy_source_rid_from_blob (peer->get_routing_id (), out_);
}
}

static bool router_debug_enabled ()
{
    return std::getenv ("ZLINK_ROUTER_DEBUG") != NULL;
}

zlink::router_t::router_t (class ctx_t *parent_, uint32_t tid_, int sid_) :
    routing_socket_base_t (parent_, tid_, sid_),
    _prefetched (false),
    _routing_id_sent (false),
    _current_in (NULL),
    _terminate_current_in (false),
    _more_in (false),
    _current_out (NULL),
    _more_out (false),
    _next_integral_routing_id (generate_random ()),
    _mandatory (true),
    _probe_router (false),
    _handover (true),
    _dispatch_source_rid_valid (false)
{
    options.type = ZLINK_CORE_SOCKET_ROUTER;
    options.recv_routing_id = true;
    options.can_send_hello_msg = true;
    options.can_recv_disconnect_msg = true;

    _prefetched_id.init ();
    _prefetched_msg.init ();
}

zlink::router_t::~router_t ()
{
    zlink_assert (_anonymous_pipes.empty ());
    close_socket_msg_parts (&_dispatch_parts);
    clear_dispatch_source_rid (&_dispatch_source_rid,
                               &_dispatch_source_rid_valid);
    _prefetched_id.close ();
    _prefetched_msg.close ();
}

void zlink::router_t::xattach_pipe (pipe_t *pipe_,
                                  bool subscribe_to_all_,
                                  bool locally_initiated_)
{
    LIBZLINK_UNUSED (subscribe_to_all_);

    zlink_assert (pipe_);

    if (_probe_router) {
        msg_t probe_msg;
        int rc = probe_msg.init ();
        errno_assert (rc == 0);

        rc = pipe_->write_and_flush (&probe_msg);
        // zlink_assert (rc) is not applicable here, since it is not a bug.
        LIBZLINK_UNUSED (rc);

        rc = probe_msg.close ();
        errno_assert (rc == 0);
    }

    const bool routing_id_ok = identify_peer (pipe_, locally_initiated_);
    if (router_debug_enabled ()) {
        fprintf (stderr,
                 "router xattach_pipe: pipe=%p local=%d routing_id_ok=%d\n",
                 static_cast<void *> (pipe_), locally_initiated_ ? 1 : 0,
                 routing_id_ok ? 1 : 0);
    }
    if (routing_id_ok) {
        _fq.attach (pipe_);
        if (local_admission_state () != ZLINK_ADMISSION_SERVING)
            send_local_admission_state (pipe_);
        (void) pipe_->check_read ();
        if (socket_msg_dispatch_active ()) {
            _fq.deactivate (pipe_);
        }
    } else
        _anonymous_pipes.insert (pipe_);
}

int zlink::router_t::xsetsockopt (int option_,
                                const void *optval_,
                                size_t optvallen_)
{
    const bool is_int = (optvallen_ == sizeof (int));
    int value = 0;
    if (is_int)
        memcpy (&value, optval_, sizeof (int));

    switch (option_) {
        case ZLINK_INTERNAL_OPT_ROUTER_MANDATORY:
            if (is_int && value >= 0) {
                _mandatory = (value != 0);
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_PROBE_ROUTER:
            if (is_int && value >= 0) {
                _probe_router = (value != 0);
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_ROUTER_HANDOVER:
            if (is_int && value >= 0) {
                _handover = (value != 0);
                return 0;
            }
            break;

        default:
            return routing_socket_base_t::xsetsockopt (option_, optval_,
                                                       optvallen_);
    }
    errno = EINVAL;
    return -1;
}

int zlink::router_t::xgetsockopt (int option_,
                                  void *optval_,
                                  size_t *optvallen_)
{
    if (!optval_ || !optvallen_ || *optvallen_ != sizeof (int)) {
        errno = EINVAL;
        return -1;
    }

    int *value = static_cast<int *> (optval_);
    switch (option_) {
        case ZLINK_INTERNAL_OPT_ROUTER_MANDATORY:
            *value = _mandatory ? 1 : 0;
            return 0;
        case ZLINK_INTERNAL_OPT_PROBE_ROUTER:
            *value = _probe_router ? 1 : 0;
            return 0;
        case ZLINK_INTERNAL_OPT_ROUTER_HANDOVER:
            *value = _handover ? 1 : 0;
            return 0;
        default:
            return routing_socket_base_t::xgetsockopt (option_, optval_,
                                                       optvallen_);
    }
}


void zlink::router_t::xpipe_terminated (pipe_t *pipe_)
{
    if (router_debug_enabled ()) {
        char rid_text[160];
        format_blob_routing_id_debug (pipe_->get_routing_id (), rid_text,
                                      sizeof (rid_text));
        fprintf (stderr,
                 "router xpipe_terminated: pipe=%p rid=%s anonymous=%d\n",
                 static_cast<void *> (pipe_), rid_text,
                 _anonymous_pipes.count (pipe_) != 0 ? 1 : 0);
    }
    if (0 == _anonymous_pipes.erase (pipe_)) {
        erase_out_pipe (pipe_);
        _fq.pipe_terminated (pipe_);
        pipe_->rollback ();
        if (pipe_ == _current_out)
            _current_out = NULL;
    }
}

void zlink::router_t::xread_activated (pipe_t *pipe_)
{
    const std::set<pipe_t *>::iterator it = _anonymous_pipes.find (pipe_);
    if (router_debug_enabled ()) {
        char rid_text[160];
        format_blob_routing_id_debug (pipe_->get_routing_id (), rid_text,
                                      sizeof (rid_text));
        fprintf (stderr,
                 "router xread_activated: pipe=%p anonymous=%d pipe_rid=%s\n",
                 static_cast<void *> (pipe_), it != _anonymous_pipes.end () ? 1 : 0,
                 rid_text);
    }
    if (it == _anonymous_pipes.end ())
        _fq.activated (pipe_);
    else {
        const bool routing_id_ok = identify_peer (pipe_, false);
        if (router_debug_enabled ()) {
            fprintf (stderr,
                     "router xread_activated identify_peer: pipe=%p ok=%d\n",
                     static_cast<void *> (pipe_), routing_id_ok ? 1 : 0);
        }
        if (routing_id_ok) {
            _anonymous_pipes.erase (it);
            _fq.attach (pipe_);
            (void) pipe_->check_read ();
        }
    }

    if (!socket_msg_dispatch_active ())
        return;

    msg_t msg;
    const int init_rc = msg.init ();
    errno_assert (init_rc == 0);

    pipe_t *dispatch_pipe = NULL;
    while (_fq.recvpipe (&msg, &dispatch_pipe) == 0) {
        const int dispatch_rc = xsocket_msg_dispatch (&msg, dispatch_pipe);
        if (dispatch_rc <= 0)
            break;
    }

    const int close_rc = msg.close ();
    errno_assert (close_rc == 0);
}

int zlink::router_t::xsend (msg_t *msg_)
{
    //  If this is the first part of the message it's the ID of the
    //  peer to send the message to.
    if (!_more_out) {
        zlink_assert (!_current_out);

        //  If we have malformed message (prefix with no subsequent message)
        //  then just silently ignore it.
        //  TODO: The connections should be killed instead.
        if (msg_->flags () & msg_t::more) {
            _more_out = true;

            //  Find the pipe associated with the routing id stored in the prefix.
            //  If there's no such pipe just silently ignore the message, unless
            //  router_mandatory is set.
            out_pipe_t *out_pipe = lookup_out_pipe (
              blob_t (static_cast<unsigned char *> (msg_->data ()),
                      msg_->size (), zlink::reference_tag_t ()));

            if (out_pipe) {
                if (out_pipe->admission_state == ZLINK_ADMISSION_DRAINING) {
                    _more_out = false;
                    errno = ECONNREFUSED;
                    return -1;
                }
                _current_out = out_pipe->pipe;

                // Check whether pipe is closed or not
                const pipe_write_status_t write_status =
                  _current_out->check_write_status ();
                if (write_status != pipe_write_ready) {
                    const bool pipe_full =
                      write_status == pipe_write_hwm_full;
                    out_pipe->active = false;
                    _current_out = NULL;

                    if (_mandatory) {
                        _more_out = false;
                        if (pipe_full)
                            errno = EAGAIN;
                        else
                            errno = EHOSTUNREACH;
                        if (router_debug_enabled ()) {
                            fprintf (stderr,
                                     "router xsend: pipe not writable "
                                     "(mandatory) size=%zu errno=%d\n",
                                     msg_->size (), errno);
                        }
                        return -1;
                    }
                }
            } else if (_mandatory) {
                _more_out = false;
                errno = EHOSTUNREACH;
                if (router_debug_enabled ()) {
                    fprintf (stderr,
                             "router xsend: no out pipe (mandatory) "
                             "rid_size=%zu errno=%d\n",
                             msg_->size (), errno);
                }
                return -1;
            }
        }

        int rc = msg_->close ();
        errno_assert (rc == 0);
        rc = msg_->init ();
        errno_assert (rc == 0);
        return 0;
    }

    //  Check whether this is the last part of the message.
    _more_out = (msg_->flags () & msg_t::more) != 0;

    //  Push the message into the pipe. If there's no out pipe, just drop it.
    if (_current_out) {
        const bool ok = _more_out ? _current_out->write (msg_)
                                  : _current_out->write_and_flush (msg_);
        if (unlikely (!ok)) {
            if (router_debug_enabled ()) {
                fprintf (stderr,
                         "router xsend: drop message size=%zu\n",
                         msg_->size ());
            }
            // Message failed to send - we must close it ourselves.
            const int rc = msg_->close ();
            errno_assert (rc == 0);
            // HWM was checked before, so the pipe must be gone. Roll back
            // messages that were piped, for example REP labels.
            _current_out->rollback ();
            _current_out = NULL;
        } else {
            if (!_more_out) {
                _current_out = NULL;
            }
        }
    } else {
        if (router_debug_enabled ()) {
            fprintf (stderr,
                     "router xsend: no current out, drop size=%zu\n",
                     msg_->size ());
        }
        const int rc = msg_->close ();
        errno_assert (rc == 0);
    }

    //  Detach the message from the data buffer.
    const int rc = msg_->init ();
    errno_assert (rc == 0);

    return 0;
}

int zlink::router_t::xsend_routed (const zlink_routing_id_t *target_rid_,
                                   msg_t *msg_)
{
    zlink_assert (!_more_out);
    zlink_assert (!_current_out);

    _more_out = (msg_->flags () & msg_t::more) != 0;

    out_pipe_t *out_pipe = lookup_out_pipe (
      blob_t (const_cast<unsigned char *> (target_rid_->data),
              target_rid_->size, zlink::reference_tag_t ()));
    if (out_pipe) {
        if (out_pipe->admission_state == ZLINK_ADMISSION_DRAINING) {
            _more_out = false;
            errno = ECONNREFUSED;
            if (router_debug_enabled ()) {
                fprintf (stderr,
                         "router xsend_routed: draining rid_size=%u\n",
                         static_cast<unsigned> (target_rid_->size));
            }
            return -1;
        }
        _current_out = out_pipe->pipe;

        const pipe_write_status_t write_status =
          _current_out->check_write_status ();
        if (write_status != pipe_write_ready) {
            const bool pipe_full = write_status == pipe_write_hwm_full;
            out_pipe->active = false;
            _current_out = NULL;

            if (_mandatory) {
                _more_out = false;
                errno = pipe_full ? EAGAIN : EHOSTUNREACH;
                if (router_debug_enabled ()) {
                    fprintf (stderr,
                             "router xsend_routed: pipe not writable "
                             "rid_size=%u errno=%d\n",
                             static_cast<unsigned> (target_rid_->size),
                             errno);
                }
                return -1;
            }
        }
    } else if (_mandatory) {
        _more_out = false;
        errno = EHOSTUNREACH;
                if (router_debug_enabled ()) {
                    char rid_text[160];
                    format_routing_id_debug (target_rid_, rid_text,
                                             sizeof (rid_text));
                    fprintf (stderr,
                             "router xsend_routed: no out pipe rid_size=%u "
                             "rid=%s\n",
                             static_cast<unsigned> (target_rid_->size),
                             rid_text);
                }
        return -1;
    }

    if (_current_out) {
        const bool ok = _more_out ? _current_out->write (msg_)
                                  : _current_out->write_and_flush (msg_);
        if (unlikely (!ok)) {
            if (router_debug_enabled ()) {
                fprintf (stderr,
                         "router xsend_routed: write failed rid_size=%u\n",
                         static_cast<unsigned> (target_rid_->size));
            }
            const int rc = msg_->close ();
            errno_assert (rc == 0);
            _current_out->rollback ();
            _current_out = NULL;
        } else if (!_more_out) {
            _current_out = NULL;
        }
    } else {
        const int rc = msg_->close ();
        errno_assert (rc == 0);
    }

    const int rc = msg_->init ();
    errno_assert (rc == 0);
    return 0;
}

int zlink::router_t::xrecv (msg_t *msg_)
{
    if (_prefetched) {
        if (!_routing_id_sent) {
            const int rc = msg_->move (_prefetched_id);
            errno_assert (rc == 0);
            _routing_id_sent = true;
        } else {
            const int rc = msg_->move (_prefetched_msg);
            errno_assert (rc == 0);
            _prefetched = false;
        }
        _more_in = (msg_->flags () & msg_t::more) != 0;

        if (!_more_in) {
            if (_terminate_current_in) {
                _current_in->terminate (true);
                _terminate_current_in = false;
            }
            _current_in = NULL;
        }
        return 0;
    }

    pipe_t *pipe = NULL;
    int rc = _fq.recvpipe (msg_, &pipe);

    //  It's possible that we receive peer's routing id. That happens
    //  after reconnection. The current implementation assumes that
    //  the peer always uses the same routing id.
    while (rc == 0 && msg_->is_routing_id ())
        rc = _fq.recvpipe (msg_, &pipe);

    if (rc != 0)
        return -1;

    zlink_assert (pipe != NULL);

    //  If we are in the middle of reading a message, just return the next part.
    if (_more_in) {
        _more_in = (msg_->flags () & msg_t::more) != 0;

        if (!_more_in) {
            if (_terminate_current_in) {
                _current_in->terminate (true);
                _terminate_current_in = false;
            }
            _current_in = NULL;
        }
    } else {
        //  We are at the beginning of a message.
        //  Keep the message part we have in the prefetch buffer
        //  and return the ID of the peer instead.
        rc = _prefetched_msg.move (*msg_);
        errno_assert (rc == 0);
        _prefetched = true;
        _current_in = pipe;

        const blob_t &routing_id = pipe->get_routing_id ();
        rc = msg_->init_size (routing_id.size ());
        errno_assert (rc == 0);
        memcpy (msg_->data (), routing_id.data (), routing_id.size ());
        msg_->set_flags (msg_t::more);
        _routing_id_sent = true;
    }

    return 0;
}

int zlink::router_t::xrecv_routed (msg_t *msg_,
                                   zlink_routing_id_t *source_rid_out_)
{
    if (_prefetched) {
        if (source_rid_out_)
            copy_router_pipe_source_rid (_current_in, source_rid_out_);

        const int rc = msg_->move (_prefetched_msg);
        errno_assert (rc == 0);
        _prefetched = false;
        _routing_id_sent = true;
        _more_in = (msg_->flags () & msg_t::more) != 0;

        if (!_more_in) {
            if (_terminate_current_in) {
                _current_in->terminate (true);
                _terminate_current_in = false;
            }
            _current_in = NULL;
        }
        return 0;
    }

    pipe_t *pipe = NULL;
    int rc = _fq.recvpipe (msg_, &pipe);

    while (rc == 0 && msg_->is_routing_id ())
        rc = _fq.recvpipe (msg_, &pipe);

    if (rc != 0)
        return -1;

    zlink_assert (pipe != NULL);

    if (!_more_in) {
        _current_in = pipe;
        if (source_rid_out_)
            copy_router_pipe_source_rid (pipe, source_rid_out_);
        _routing_id_sent = true;
    } else if (_current_in && source_rid_out_) {
        copy_router_pipe_source_rid (_current_in, source_rid_out_);
    }

    _more_in = (msg_->flags () & msg_t::more) != 0;

    if (!_more_in) {
        if (_terminate_current_in) {
            _current_in->terminate (true);
            _terminate_current_in = false;
        }
        _current_in = NULL;
    }

    return 0;
}

int zlink::router_t::xsocket_msg_dispatch (msg_t *msg_, pipe_t *pipe_)
{
    if (!socket_msg_dispatch_active ())
        return 0;

    if (msg_->is_routing_id ()) {
        pipe_t *socket_pipe = pipe_;
        if (socket_pipe && socket_pipe->get_peer ())
            socket_pipe = socket_pipe->get_peer ();

        const bool needs_route_registration =
          socket_pipe
          && (_anonymous_pipes.count (socket_pipe) != 0
              || lookup_out_pipe (blob_t (
                   const_cast<unsigned char *> (
                     static_cast<unsigned char *> (msg_->data ())),
                   msg_->size (), zlink::reference_tag_t ()))
                   == NULL);
        if (needs_route_registration) {
            blob_t routing_id (static_cast<unsigned char *> (msg_->data ()),
                               msg_->size ());
            if (adopt_peer_routing_id (socket_pipe, ZLINK_MOVE (routing_id)))
                promote_anonymous_pipe_for_dispatch (socket_pipe);
        }
        store_dispatch_source_rid (&_dispatch_source_rid,
                                   &_dispatch_source_rid_valid, msg_);
        return 1;
    }

    store_socket_msg_part (&_dispatch_parts, msg_);
    if ((reinterpret_cast<msg_t *> (&_dispatch_parts.back ())->flags ()
         & msg_t::more)
        != 0) {
        return 1;
    }

    zlink_socket_msg_handler_fn handler = socket_msg_handler ();
    if (!handler) {
        close_socket_msg_parts (&_dispatch_parts);
        clear_dispatch_source_rid (&_dispatch_source_rid,
                                   &_dispatch_source_rid_valid);
        return 1;
    }

    zlink_routing_id_t source_rid;
    copy_router_pipe_source_rid (pipe_, &source_rid);

    invoke_socket_msg_handler (handler, &source_rid, &_dispatch_parts[0],
                               _dispatch_parts.size ());
    _dispatch_parts.clear ();
    clear_dispatch_source_rid (&_dispatch_source_rid,
                               &_dispatch_source_rid_valid);
    return 1;
}

void zlink::router_t::xarm_socket_msg_dispatch ()
{
    _fq.arm_dispatch ();
}

void zlink::router_t::xdispatch_io ()
{
    if (!socket_msg_dispatch_active ())
        return;
    zlink::drain_socket_dispatch_loop (
      [this] (msg_t *msg_, pipe_t **pipe_out_) {
          return _fq.recvpipe (msg_, pipe_out_);
      },
      [this] (msg_t *msg_, pipe_t *pipe_) {
          return xsocket_msg_dispatch (msg_, pipe_);
      });
}

int zlink::router_t::rollback ()
{
    if (_current_out) {
        _current_out->rollback ();
        _current_out = NULL;
        _more_out = false;
    }
    return 0;
}

bool zlink::router_t::xhas_in ()
{
    //  If we are in the middle of reading the messages, there are
    //  definitely more parts available.
    if (_more_in)
        return true;

    //  We may already have a message pre-fetched.
    if (_prefetched)
        return true;

    //  Try to read the next message.
    //  The message, if read, is kept in the pre-fetch buffer.
    pipe_t *pipe = NULL;
    int rc = _fq.recvpipe (&_prefetched_msg, &pipe);

    //  It's possible that we receive peer's routing id. That happens
    //  after reconnection. The current implementation assumes that
    //  the peer always uses the same routing id.
    //  TODO: handle the situation when the peer changes its routing id.
    while (rc == 0 && _prefetched_msg.is_routing_id ())
        rc = _fq.recvpipe (&_prefetched_msg, &pipe);

    if (rc != 0)
        return false;

    zlink_assert (pipe != NULL);

    const blob_t &routing_id = pipe->get_routing_id ();
    rc = _prefetched_id.init_size (routing_id.size ());
    errno_assert (rc == 0);
    memcpy (_prefetched_id.data (), routing_id.data (), routing_id.size ());
    _prefetched_id.set_flags (msg_t::more);

    _prefetched = true;
    _routing_id_sent = false;
    _current_in = pipe;

    return true;
}

static bool check_pipe_hwm (const zlink::pipe_t &pipe_)
{
    return pipe_.check_hwm ();
}

bool zlink::router_t::xhas_out ()
{
    //  In theory, ROUTER socket is always ready for writing (except when
    //  MANDATORY is set). Whether actual attempt to write succeeds depends
    //  on which pipe the message is going to be routed to.

    if (!_mandatory)
        return true;

    return any_of_out_pipes ([] (const out_pipe_t &out_pipe_) {
        return out_pipe_.admission_state == ZLINK_ADMISSION_SERVING
               && check_pipe_hwm (*out_pipe_.pipe);
    });
}

int zlink::router_t::get_peer_state (const void *routing_id_,
                                   size_t routing_id_size_) const
{
    int res = 0;

    // TODO remove the const_cast, see comment in lookup_out_pipe
    const blob_t routing_id_blob (
      static_cast<unsigned char *> (const_cast<void *> (routing_id_)),
      routing_id_size_, reference_tag_t ());
    const out_pipe_t *out_pipe = lookup_out_pipe (routing_id_blob);
    if (!out_pipe) {
        errno = EHOSTUNREACH;
        return -1;
    }

    if (out_pipe->admission_state != ZLINK_ADMISSION_SERVING)
        return 0;

    if (out_pipe->pipe->check_hwm ())
        res |= ZLINK_POLLOUT;

    /** \todo does it make any sense to check the inpipe as well? */

    return res;
}
