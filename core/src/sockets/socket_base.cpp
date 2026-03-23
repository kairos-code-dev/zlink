/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <algorithm>
#include <new>

#include "sockets/socket_base.hpp"
#include "core/ctx.hpp"
#include "core/mailbox.hpp"
#include "utils/err.hpp"
#include "utils/random.hpp"

#include "sockets/pair.hpp"
#include "sockets/pub.hpp"
#include "sockets/sub.hpp"
#include "sockets/dealer.hpp"
#include "sockets/router.hpp"
#include "sockets/stream.hpp"
#include "sockets/xpub.hpp"
#include "sockets/xsub.hpp"

namespace
{
static void generate_default_routing_id (unsigned char out_[16])
{
    zlink::generate_random_bytes (out_, 16);

    // RFC 4122 variant/version layout.
    out_[6] = static_cast<unsigned char> ((out_[6] & 0x0F) | 0x40);
    out_[8] = static_cast<unsigned char> ((out_[8] & 0x3F) | 0x80);

    bool all_zero = true;
    for (size_t i = 0; i < 16; ++i) {
        if (out_[i] != 0) {
            all_zero = false;
            break;
        }
    }
    if (all_zero)
        out_[15] = 1;
}
}

bool zlink::socket_base_t::check_tag () const
{
    return _tag == 0xbaddecaf;
}

zlink::socket_base_t *zlink::socket_base_t::create (int type_,
                                                class ctx_t *parent_,
                                                uint32_t tid_,
                                                int sid_)
{
    socket_base_t *s = NULL;
    switch (type_) {
        case ZLINK_CORE_SOCKET_PAIR:
            s = new (std::nothrow) pair_t (parent_, tid_, sid_);
            break;
        case ZLINK_CORE_SOCKET_PUB:
            s = new (std::nothrow) pub_t (parent_, tid_, sid_);
            break;
        case ZLINK_CORE_SOCKET_SUB:
            s = new (std::nothrow) sub_t (parent_, tid_, sid_);
            break;
        case ZLINK_CORE_SOCKET_DEALER:
            s = new (std::nothrow) dealer_t (parent_, tid_, sid_);
            break;
        case ZLINK_CORE_SOCKET_ROUTER:
            s = new (std::nothrow) router_t (parent_, tid_, sid_);
            break;
        case ZLINK_CORE_SOCKET_STREAM:
            s = new (std::nothrow) stream_t (parent_, tid_, sid_);
            break;
        case ZLINK_CORE_SOCKET_XPUB:
            s = new (std::nothrow) xpub_t (parent_, tid_, sid_);
            break;
        case ZLINK_CORE_SOCKET_XSUB:
            s = new (std::nothrow) xsub_t (parent_, tid_, sid_);
            break;
        default:
            errno = EINVAL;
            return NULL;
    }

    alloc_assert (s);

    if (s->_mailbox == NULL) {
        s->_destroyed = true;
        LIBZLINK_DELETE (s);
        return NULL;
    }

    return s;
}

zlink::socket_base_t::socket_base_t (ctx_t *parent_,
                                   uint32_t tid_,
                                   int sid_) :
    own_t (parent_, tid_),
    _tag (0xbaddecaf),
    _ctx_terminated (false),
    _destroyed (false),
    _poller (NULL),
    _last_tsc (0),
    _ticks (0),
    _rcvmore (false),
    _runtime (),
    _endpoints (_runtime.endpoint_registry.endpoints),
    _inprocs (_runtime.endpoint_registry.inprocs),
    _monitor_socket (_runtime.monitor_bridge.socket),
    _monitor_events (_runtime.monitor_bridge.events),
    _monitor_events_atomic (_runtime.monitor_bridge.events_atomic),
    _monitor_lossy (_runtime.monitor_bridge.lossy),
    _mailbox_refcnt (_runtime.lifecycle_hooks.mailbox_refcnt),
    _destroy_pending (_runtime.lifecycle_hooks.destroy_pending),
    _monitor_async_mailbox_owned (
      _runtime.lifecycle_hooks.monitor_async_mailbox_owned),
    _async_mailbox_active (_runtime.lifecycle_hooks.async_mailbox_active),
    _async_quiesce_pending (_runtime.lifecycle_hooks.async_quiesce_pending),
    _async_processing_done (_runtime.lifecycle_hooks.async_processing_done),
    _async_done_mu (_runtime.lifecycle_hooks.async_done_mu),
    _async_done_cv (_runtime.lifecycle_hooks.async_done_cv),
    _socket_msg_handler (_runtime.dispatch_bridge.socket_msg_handler),
    _socket_msg_handler_subject (
      _runtime.dispatch_bridge.socket_msg_handler_subject),
    _socket_msg_handler_userdata (
      _runtime.dispatch_bridge.socket_msg_handler_userdata),
    _spot_handler (_runtime.dispatch_bridge.spot_handler),
    _spot_handler_userdata (_runtime.dispatch_bridge.spot_handler_userdata),
    _public_api_state (_runtime.dispatch_bridge.public_api_state),
    _public_api_sync (_runtime.dispatch_bridge.public_api_sync),
    _callback_api_depth (_runtime.dispatch_bridge.callback_api_depth),
    _close_deferred (_runtime.dispatch_bridge.close_deferred),
    _send_ready_handler (_runtime.dispatch_bridge.send_ready_handler),
    _send_ready_handler_subject (
      _runtime.dispatch_bridge.send_ready_handler_subject),
    _send_ready_handler_userdata (
      _runtime.dispatch_bridge.send_ready_handler_userdata),
    _send_ready_seq (_runtime.dispatch_bridge.send_ready_seq),
    _send_ready_writer_sync (_runtime.dispatch_bridge.send_ready_writer_sync),
    _send_ready_armed (_runtime.dispatch_bridge.send_ready_armed),
    _socket_msg_dispatch_sync (
      _runtime.dispatch_bridge.socket_msg_dispatch_sync),
    _last_recv_source_rid (_runtime.dispatch_bridge.last_recv_source_rid),
    _last_recv_source_rid_valid (
      _runtime.dispatch_bridge.last_recv_source_rid_valid),
    _monitor_sync (_runtime.monitor_bridge.sync),
    _monitor_queue_sync (_runtime.monitor_bridge.queue_sync),
    _monitor_queue_cv (_runtime.monitor_bridge.queue_cv),
    _monitor_queue (_runtime.monitor_bridge.queue),
    _monitor_queue_stop (_runtime.monitor_bridge.queue_stop),
    _monitor_thread (_runtime.monitor_bridge.thread),
    _monitor_thread_started (_runtime.monitor_bridge.thread_started),
    _disconnected (false)
{
    _term_pipe_acks_registered = 0;
    _term_pipe_acks_received = 0;
    options.socket_id = sid_;
    options.ipv6 = (parent_->get (ZLINK_INTERNAL_OPT_IPV6) != 0);
    options.linger.store (parent_->get (ZLINK_INTERNAL_OPT_BLOCKY) ? -1 : 0);

    if (options.routing_id_size == 0) {
        unsigned char buf[16];
        generate_default_routing_id (buf);
        memcpy (options.routing_id, buf, sizeof buf);
        options.routing_id_size = static_cast<unsigned char> (sizeof buf);
    }

    mailbox_t *m = new (std::nothrow) mailbox_t ();
    zlink_assert (m);
    _mailbox = m;
}

static void copy_routing_id (zlink_routing_id_t *out_,
                             const zlink::blob_t &routing_id_)
{
    if (!out_)
        return;
    const size_t copy_size =
      std::min (routing_id_.size (), sizeof (out_->data));
    out_->size = static_cast<uint8_t> (copy_size);
    if (copy_size > 0)
        memcpy (out_->data, routing_id_.data (), copy_size);
}

zlink::socket_base_t::~socket_base_t ()
{
    if (_mailbox)
        LIBZLINK_DELETE (_mailbox);

    scoped_lock_t lock (_monitor_sync);
    stop_monitor ();

    zlink_assert (_destroyed);
}

zlink::i_mailbox *zlink::socket_base_t::get_mailbox () const
{
    return _mailbox;
}

void zlink::socket_base_t::stop ()
{
    //  Called by ctx when it is terminated (zlink_ctx_term).
    //  'stop' command is sent from the threads that called zlink_ctx_term to
    //  the thread owning the socket. This way, blocking call in the
    //  owner thread can be interrupted.
    send_stop ();
}


int zlink::socket_base_t::xsetsockopt (int, const void *, size_t)
{
    errno = EINVAL;
    return -1;
}

int zlink::socket_base_t::xgetsockopt (int, void *, size_t *)
{
    errno = EINVAL;
    return -1;
}

bool zlink::socket_base_t::xhas_out ()
{
    return false;
}

int zlink::socket_base_t::xsend (msg_t *)
{
    errno = ENOTSUP;
    return -1;
}

int zlink::socket_base_t::xsend_routed (const zlink_routing_id_t *target_rid_,
                                        msg_t *msg_)
{
    LIBZLINK_UNUSED (target_rid_);
    LIBZLINK_UNUSED (msg_);
    errno = ENOTSUP;
    return -1;
}

int zlink::socket_base_t::xrollback ()
{
    return 0;
}

bool zlink::socket_base_t::xhas_in ()
{
    return false;
}

int zlink::socket_base_t::xjoin (const char *group_)
{
    LIBZLINK_UNUSED (group_);
    errno = ENOTSUP;
    return -1;
}

int zlink::socket_base_t::xleave (const char *group_)
{
    LIBZLINK_UNUSED (group_);
    errno = ENOTSUP;
    return -1;
}

int zlink::socket_base_t::xrecv (msg_t *)
{
    errno = ENOTSUP;
    return -1;
}

int zlink::socket_base_t::xrecv_routed (msg_t *msg_,
                                        zlink_routing_id_t *source_rid_out_)
{
    if (source_rid_out_)
        memset (source_rid_out_, 0, sizeof (*source_rid_out_));

    const int rc = xrecv (msg_);
    if (rc == 0 && source_rid_out_)
        copy_last_recv_source_rid (source_rid_out_);
    return rc;
}

void zlink::socket_base_t::xread_activated (pipe_t *)
{
    zlink_assert (false);
}
void zlink::socket_base_t::xwrite_activated (pipe_t *)
{
    zlink_assert (false);
}

void zlink::socket_base_t::xhiccuped (pipe_t *)
{
    zlink_assert (false);
}
