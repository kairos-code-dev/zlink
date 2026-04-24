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
    _handover (options.rid_duplicate_policy == ZLINK_RID_DUPLICATE_HANDOVER),
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
        case ZLINK_INTERNAL_OPT_RID_DUPLICATE_POLICY:
            if (is_int && value >= 0) {
                if (option_ == ZLINK_INTERNAL_OPT_RID_DUPLICATE_POLICY
                    && value != ZLINK_RID_DUPLICATE_REJECT
                    && value != ZLINK_RID_DUPLICATE_HANDOVER)
                    break;
                _handover = option_ == ZLINK_INTERNAL_OPT_RID_DUPLICATE_POLICY
                               ? value == ZLINK_RID_DUPLICATE_HANDOVER
                               : value != 0;
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
        case ZLINK_INTERNAL_OPT_RID_DUPLICATE_POLICY:
            *value = _handover ? ZLINK_RID_DUPLICATE_HANDOVER
                               : ZLINK_RID_DUPLICATE_REJECT;
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

int zlink::router_t::rollback ()
{
    if (_current_out) {
        _current_out->rollback ();
        _current_out = NULL;
        _more_out = false;
    }
    return 0;
}
