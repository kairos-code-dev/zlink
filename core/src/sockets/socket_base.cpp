/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "core/send_internal.hpp"
#include <new>
#include <string>
#include <algorithm>
#include <ctime>

#include "utils/macros.hpp"

#if defined ZLINK_HAVE_WINDOWS
#if defined _MSC_VER
#if defined _WIN32_WCE
#include <cmnintrin.h>
#else
#include <intrin.h>
#endif
#endif
#else
#include <unistd.h>
#include <ctype.h>
#endif

#include "sockets/socket_base.hpp"
#include "protocol/wire.hpp"
#include "zlink.h"
#include "utils/err.hpp"
#include "utils/random.hpp"
#include "utils/sleep.hpp"

// ASIO-only build: Transport listeners are always included
#include "transports/tcp/asio_tcp_listener.hpp"
#if defined ZLINK_HAVE_IPC
#include "transports/ipc/asio_ipc_listener.hpp"
#endif
#if defined ZLINK_HAVE_ASIO_SSL
#include "transports/tls/asio_tls_listener.hpp"
#endif

#if defined ZLINK_HAVE_WS
#include "transports/ws/asio_ws_listener.hpp"
#include "transports/ws/ws_address.hpp"
#endif
#include "core/io_thread.hpp"
#include "core/session_base.hpp"
#include "utils/config.hpp"
#include "core/pipe.hpp"
#include "utils/err.hpp"
#include "core/ctx.hpp"
#include "utils/likely.hpp"
#include "core/msg.hpp"
#include "core/address.hpp"
#include "transports/ipc/ipc_address.hpp"
#include "transports/tcp/tcp_address.hpp"
#ifdef ZLINK_HAVE_OPENPGM
#include "transports/pgm/pgm_socket.hpp"
#endif
#include "core/mailbox.hpp"

#include <boost/asio.hpp>

#ifdef ZLINK_HAVE_WSS
#include "transports/tls/wss_address.hpp"
#endif

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
std::string make_monitor_ready_key (
  const zlink::endpoint_uri_pair_t &endpoint_uri_pair_,
  const unsigned char *routing_id_,
  size_t routing_id_size_)
{
    std::string key = endpoint_uri_pair_.identifier ();
    if (key.empty ())
        key = endpoint_uri_pair_.remote;
    key.push_back ('\0');
    if (routing_id_ && routing_id_size_ > 0)
        key.append (reinterpret_cast<const char *> (routing_id_),
                    routing_id_size_);
    return key;
}

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

static const uint32_t public_api_closing_bit = 0x80000000u;
static const uint32_t public_api_inflight_mask = ~public_api_closing_bit;
}

void zlink::socket_inprocs_t::emplace (const char *endpoint_uri_, pipe_t *pipe_)
{
    _inprocs.ZLINK_MAP_INSERT_OR_EMPLACE (std::string (endpoint_uri_), pipe_);
}

int zlink::socket_inprocs_t::erase_pipes (
  const std::string &endpoint_uri_str_)
{
    const std::pair<map_t::iterator, map_t::iterator> range =
      _inprocs.equal_range (endpoint_uri_str_);
    if (range.first == range.second) {
        errno = ENOENT;
        return -1;
    }

    for (map_t::iterator it = range.first; it != range.second; ++it) {
        it->second->send_disconnect_msg ();
        // Explicit endpoint disconnect should not defer pipe teardown.
        // The non-inproc term_endpoint path also uses terminate(false).
        it->second->terminate (false);
    }
    _inprocs.erase (range.first, range.second);
    return 0;
}

void zlink::socket_inprocs_t::erase_pipe (const pipe_t *pipe_)
{
    for (map_t::iterator it = _inprocs.begin (), end = _inprocs.end ();
         it != end; ++it)
        if (it->second == pipe_) {
            _inprocs.erase (it);
            break;
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


zlink::routing_socket_base_t::routing_socket_base_t (class ctx_t *parent_,
                                                   uint32_t tid_,
                                                   int sid_) :
    socket_base_t (parent_, tid_, sid_)
{
}

zlink::routing_socket_base_t::~routing_socket_base_t ()
{
    zlink_assert (_out_pipes.empty ());
}

int zlink::routing_socket_base_t::xsetsockopt (int option_,
                                             const void *optval_,
                                             size_t optvallen_)
{
    switch (option_) {
        case ZLINK_INTERNAL_OPT_CONNECT_ROUTING_ID:
            // TODO why isn't it possible to set an empty connect_routing_id
            //   (which is the default value)
            if (optval_ && optvallen_) {
                _connect_routing_id.assign (static_cast<const char *> (optval_),
                                            optvallen_);
                return 0;
            }
            break;
    }
    errno = EINVAL;
    return -1;
}

void zlink::routing_socket_base_t::xwrite_activated (pipe_t *pipe_)
{
    const out_pipes_t::iterator end = _out_pipes.end ();
    out_pipes_t::iterator it;
    for (it = _out_pipes.begin (); it != end; ++it)
        if (it->second.pipe == pipe_)
            break;

    zlink_assert (it != end);
    // Duplicate write-activation notifications can race with async flush
    // cycles under high STREAM load. Keep activation idempotent.
    if (it->second.active)
        return;
    it->second.active = true;
}

std::string zlink::routing_socket_base_t::extract_connect_routing_id ()
{
    std::string res = ZLINK_MOVE (_connect_routing_id);
    _connect_routing_id.clear ();
    return res;
}

bool zlink::routing_socket_base_t::connect_routing_id_is_set () const
{
    return !_connect_routing_id.empty ();
}

void zlink::routing_socket_base_t::add_out_pipe (blob_t routing_id_,
                                               pipe_t *pipe_)
{
    //  Add the record into output pipes lookup table
    const out_pipe_t outpipe = {pipe_, true};
    const bool ok =
      _out_pipes.ZLINK_MAP_INSERT_OR_EMPLACE (ZLINK_MOVE (routing_id_), outpipe)
        .second;
    zlink_assert (ok);
}

bool zlink::routing_socket_base_t::has_out_pipe (const blob_t &routing_id_) const
{
    return 0 != _out_pipes.count (routing_id_);
}

zlink::routing_socket_base_t::out_pipe_t *
zlink::routing_socket_base_t::lookup_out_pipe (const blob_t &routing_id_)
{
    // TODO we could probably avoid constructor a temporary blob_t to call this function
    out_pipes_t::iterator it = _out_pipes.find (routing_id_);
    if (it != _out_pipes.end ()) {
        //  Prefetch the out_pipe structure into L1 cache for next access
        //  This improves cache efficiency as the caller will immediately use the result
#if !defined _MSC_VER
        __builtin_prefetch (&it->second, 0, 3);
#endif
        return &it->second;
    }
    return NULL;
}

const zlink::routing_socket_base_t::out_pipe_t *
zlink::routing_socket_base_t::lookup_out_pipe (const blob_t &routing_id_) const
{
    // TODO we could probably avoid constructor a temporary blob_t to call this function
    const out_pipes_t::const_iterator it = _out_pipes.find (routing_id_);
    if (it != _out_pipes.end ()) {
        //  Prefetch the out_pipe structure into L1 cache for next access
#if !defined _MSC_VER
        __builtin_prefetch (&it->second, 0, 3);
#endif
        return &it->second;
    }
    return NULL;
}

void zlink::routing_socket_base_t::erase_out_pipe (const pipe_t *pipe_)
{
    if (!pipe_)
        return;

    const size_t erased = _out_pipes.erase (pipe_->get_routing_id ());
    if (erased != 0)
        return;

    //  Routing id may have been refreshed after attach. Fall back to
    //  pointer-based lookup to keep teardown idempotent and avoid stale pipes.
    for (out_pipes_t::iterator it = _out_pipes.begin (),
                               end = _out_pipes.end ();
         it != end; ++it) {
        if (it->second.pipe == pipe_) {
            _out_pipes.erase (it);
            return;
        }
    }
}

zlink::routing_socket_base_t::out_pipe_t
zlink::routing_socket_base_t::try_erase_out_pipe (const blob_t &routing_id_)
{
    const out_pipes_t::iterator it = _out_pipes.find (routing_id_);
    out_pipe_t res = {NULL, false};
    if (it != _out_pipes.end ()) {
        res = it->second;
        _out_pipes.erase (it);
    }
    return res;
}
