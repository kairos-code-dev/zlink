/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/macros.hpp"
#include "sockets/router.hpp"
#include "sockets/socket_dispatch_loop_internal.hpp"
#include "core/pipe.hpp"
#include "protocol/wire.hpp"
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
    if (!buf_ || buf_size_ == 0)
        return;

    if (!rid_ || rid_->size == 0) {
        std::snprintf (buf_, buf_size_, "<empty>");
        return;
    }

    size_t used = 0;
    for (size_t i = 0; i < rid_->size && used + 4 < buf_size_; ++i) {
        const unsigned char c = rid_->data[i];
        const int rc = std::snprintf (buf_ + used, buf_size_ - used, "%c%02X",
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

bool router_debug_enabled ()
{
    return std::getenv ("ZLINK_ROUTER_DEBUG") != NULL;
}

bool check_pipe_hwm (const zlink::pipe_t &pipe_)
{
    return pipe_.check_hwm ();
}
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
    if (!_more_out) {
        zlink_assert (!_current_out);

        if (msg_->flags () & msg_t::more) {
            _more_out = true;

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

                const pipe_write_status_t write_status =
                  _current_out->check_write_status ();
                if (write_status != pipe_write_ready) {
                    const bool pipe_full = write_status == pipe_write_hwm_full;
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
                                     "router xsend: pipe not writable size=%zu errno=%d\n",
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
                             "router xsend: no out pipe rid_size=%zu errno=%d\n",
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

    _more_out = (msg_->flags () & msg_t::more) != 0;

    if (_current_out) {
        const bool ok = _more_out ? _current_out->write (msg_)
                                  : _current_out->write_and_flush (msg_);
        if (unlikely (!ok)) {
            if (router_debug_enabled ()) {
                fprintf (stderr,
                         "router xsend: drop message size=%zu\n",
                         msg_->size ());
            }
            const int rc = msg_->close ();
            errno_assert (rc == 0);
            _current_out->rollback ();
            _current_out = NULL;
        } else if (!_more_out) {
            _current_out = NULL;
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
                             "router xsend_routed: pipe not writable rid_size=%u errno=%d\n",
                             static_cast<unsigned> (target_rid_->size), errno);
                }
                return -1;
            }
        }
    } else if (_mandatory) {
        _more_out = false;
        errno = EHOSTUNREACH;
        if (router_debug_enabled ()) {
            char rid_text[160];
            format_routing_id_debug (target_rid_, rid_text, sizeof (rid_text));
            fprintf (stderr,
                     "router xsend_routed: no out pipe rid_size=%u rid=%s\n",
                     static_cast<unsigned> (target_rid_->size), rid_text);
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
    while (rc == 0 && msg_->is_routing_id ())
        rc = _fq.recvpipe (msg_, &pipe);

    if (rc != 0)
        return -1;

    zlink_assert (pipe != NULL);

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

bool zlink::router_t::xhas_in ()
{
    if (_more_in)
        return true;

    if (_prefetched)
        return true;

    pipe_t *pipe = NULL;
    int rc = _fq.recvpipe (&_prefetched_msg, &pipe);

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

int zlink::router_t::get_peer_state (const void *routing_id_,
                                     size_t routing_id_size_) const
{
    int res = 0;

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

    return res;
}

bool zlink::router_t::xhas_out ()
{
    if (!_mandatory)
        return true;

    return any_of_out_pipes ([] (const out_pipe_t &out_pipe_) {
        return out_pipe_.admission_state == ZLINK_ADMISSION_SERVING
               && check_pipe_hwm (*out_pipe_.pipe);
    });
}
