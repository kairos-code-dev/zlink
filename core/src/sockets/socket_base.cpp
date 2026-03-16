/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
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
#include "utils/random.hpp"

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
thread_local zlink::socket_base_t *g_current_socket_msg_dispatch_socket = NULL;
thread_local zlink::socket_base_t *g_current_send_ready_dispatch_socket = NULL;
thread_local zlink::pipe_t *g_current_socket_msg_dispatch_pipe = NULL;
thread_local void *g_current_socket_msg_dispatch_subject = NULL;
thread_local zlink_routing_id_t g_current_socket_msg_dispatch_source_rid;
thread_local bool g_current_socket_msg_dispatch_source_rid_valid = false;

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

zlink::socket_base_t::monitor_event_record_t::monitor_event_record_t () :
    event (0),
    values_count (0)
{
    memset (values, 0, sizeof (values));
    memset (&routing_id, 0, sizeof (routing_id));
}

void zlink::socket_base_t::inprocs_t::emplace (const char *endpoint_uri_,
                                             pipe_t *pipe_)
{
    _inprocs.ZLINK_MAP_INSERT_OR_EMPLACE (std::string (endpoint_uri_), pipe_);
}

int zlink::socket_base_t::inprocs_t::erase_pipes (
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

void zlink::socket_base_t::inprocs_t::erase_pipe (const pipe_t *pipe_)
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
        case ZLINK_PAIR:
            s = new (std::nothrow) pair_t (parent_, tid_, sid_);
            break;
        case ZLINK_PUB:
            s = new (std::nothrow) pub_t (parent_, tid_, sid_);
            break;
        case ZLINK_SUB:
            s = new (std::nothrow) sub_t (parent_, tid_, sid_);
            break;
        case ZLINK_DEALER:
            s = new (std::nothrow) dealer_t (parent_, tid_, sid_);
            break;
        case ZLINK_ROUTER:
            s = new (std::nothrow) router_t (parent_, tid_, sid_);
            break;
        case ZLINK_STREAM:
            s = new (std::nothrow) stream_t (parent_, tid_, sid_);
            break;
        case ZLINK_XPUB:
            s = new (std::nothrow) xpub_t (parent_, tid_, sid_);
            break;
        case ZLINK_XSUB:
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
    _async_mailbox_active (_runtime.lifecycle_hooks.async_mailbox_active),
    _async_quiesce_pending (_runtime.lifecycle_hooks.async_quiesce_pending),
    _async_processing_done (_runtime.lifecycle_hooks.async_processing_done),
    _async_done_mu (_runtime.lifecycle_hooks.async_done_mu),
    _async_done_cv (_runtime.lifecycle_hooks.async_done_cv),
    _socket_msg_handler (_runtime.dispatch_bridge.socket_msg_handler),
    _socket_msg_handler_subject (
      _runtime.dispatch_bridge.socket_msg_handler_subject),
    _spot_handler (_runtime.dispatch_bridge.spot_handler),
    _xpub_handler (_runtime.dispatch_bridge.xpub_handler),
    _public_api_state (_runtime.dispatch_bridge.public_api_state),
    _public_api_sync (_runtime.dispatch_bridge.public_api_sync),
    _callback_api_depth (_runtime.dispatch_bridge.callback_api_depth),
    _close_deferred (_runtime.dispatch_bridge.close_deferred),
    _send_ready_handler (_runtime.dispatch_bridge.send_ready_handler),
    _send_ready_handler_subject (
      _runtime.dispatch_bridge.send_ready_handler_subject),
    _send_ready_seq (_runtime.dispatch_bridge.send_ready_seq),
    _send_ready_writer_sync (_runtime.dispatch_bridge.send_ready_writer_sync),
    _send_ready_armed (_runtime.dispatch_bridge.send_ready_armed),
    _socket_msg_dispatch_sync (
      _runtime.dispatch_bridge.socket_msg_dispatch_sync),
    _monitor_sync (_runtime.monitor_bridge.sync),
    _monitor_queue_sync (_runtime.monitor_bridge.queue_sync),
    _monitor_queue_cv (_runtime.monitor_bridge.queue_cv),
    _monitor_queue (_runtime.monitor_bridge.queue),
    _monitor_queue_stop (_runtime.monitor_bridge.queue_stop),
    _monitor_thread (_runtime.monitor_bridge.thread),
    _monitor_thread_started (_runtime.monitor_bridge.thread_started),
    _disconnected (false)
{
#ifndef NDEBUG
    _term_pipe_acks_registered = 0;
    _term_pipe_acks_received = 0;
#endif
    options.socket_id = sid_;
    options.ipv6 = (parent_->get (ZLINK_IPV6) != 0);
    options.linger.store (parent_->get (ZLINK_BLOCKY) ? -1 : 0);

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

bool zlink::socket_base_t::enter_public_api ()
{
    const uint32_t old =
      _public_api_state.fetch_add (1, std::memory_order_acq_rel);
    if ((old & public_api_closing_bit) == 0)
        return true;

    const uint32_t reverted =
      _public_api_state.fetch_sub (1, std::memory_order_acq_rel);
    zlink_assert ((reverted & public_api_inflight_mask) > 0);
    errno = ESHUTDOWN;
    return false;
}

void zlink::socket_base_t::leave_public_api ()
{
    const uint32_t old =
      _public_api_state.fetch_sub (1, std::memory_order_acq_rel);
    zlink_assert ((old & public_api_inflight_mask) > 0);
}

bool zlink::socket_base_t::enter_callback_api ()
{
    if (!enter_public_api ())
        return false;

    _callback_api_depth.fetch_add (1, std::memory_order_acq_rel);
    return true;
}

void zlink::socket_base_t::leave_callback_api ()
{
    const uint32_t depth =
      _callback_api_depth.fetch_sub (1, std::memory_order_acq_rel) - 1;
    leave_public_api ();

    if (depth == 0
        && _close_deferred.load (std::memory_order_acquire)
        && public_close_requested ()) {
        finish_close_handoff ();
    }
}

bool zlink::socket_base_t::begin_close_or_fail_busy (bool from_self_callback_)
{
    uint32_t old = _public_api_state.load (std::memory_order_acquire);
    while (true) {
        if ((old & public_api_closing_bit) != 0) {
            errno = EALREADY;
            return false;
        }

        if (!from_self_callback_ && (old & public_api_inflight_mask) != 0) {
            errno = EBUSY;
            return false;
        }

        const uint32_t desired = old | public_api_closing_bit;
        if (_public_api_state.compare_exchange_weak (
              old, desired, std::memory_order_acq_rel,
              std::memory_order_acquire)) {
            if (from_self_callback_)
                _close_deferred.store (true, std::memory_order_release);
            return true;
        }
    }
}

bool zlink::socket_base_t::public_close_requested () const
{
    return (_public_api_state.load (std::memory_order_acquire)
            & public_api_closing_bit)
           != 0;
}

void zlink::socket_base_t::lock_public_api_sync ()
{
    bool expected = false;
    while (!_public_api_sync.compare_exchange_weak (
      expected, true, std::memory_order_acquire, std::memory_order_relaxed)) {
        expected = false;
    }
}

void zlink::socket_base_t::unlock_public_api_sync ()
{
    _public_api_sync.store (false, std::memory_order_release);
}

bool zlink::socket_base_t::send_ready_slot (
  zlink_send_ready_handler_fn *handler_out_, void **subject_out_) const
{
    if (!handler_out_ || !subject_out_)
        return false;

    while (true) {
        const uint32_t s1 = _send_ready_seq.load (std::memory_order_acquire);
        if ((s1 & 1u) != 0)
            continue;

        zlink_send_ready_handler_fn handler =
          _send_ready_handler.load (std::memory_order_acquire);
        void *subject =
          _send_ready_handler_subject.load (std::memory_order_acquire);
        const uint32_t s2 = _send_ready_seq.load (std::memory_order_acquire);
        if (s1 != s2 || (s2 & 1u) != 0)
            continue;

        *handler_out_ = handler;
        *subject_out_ = subject;
        return handler != NULL;
    }
}

void zlink::socket_base_t::finish_close_handoff ()
{
    _close_deferred.store (false, std::memory_order_release);

    if (_async_mailbox_active.load (std::memory_order_acquire)) {
        stop_async_mailbox_processing ();
        wait_async_quiesced (2000);
    } else if (_async_quiesce_pending.load (std::memory_order_acquire)) {
        wait_async_quiesced (2000);
    }

    if (_mailbox)
        static_cast<mailbox_t *> (_mailbox)->clear_signalers ();

    _tag = 0xdeadbeef;
    send_reap (this);
}

int zlink::socket_base_t::get_peer_state (const void *routing_id_,
                                        size_t routing_id_size_) const
{
    LIBZLINK_UNUSED (routing_id_);
    LIBZLINK_UNUSED (routing_id_size_);

    //  Only ROUTER sockets support this
    errno = ENOTSUP;
    return -1;
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

int zlink::socket_base_t::monitor_snapshot (zlink_monitor_snapshot_t *out_)
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }

    process_commands (0, false);
    memset (out_, 0, sizeof (*out_));
    out_->source_kind = ZLINK_MONITOR_SOURCE_SOCKET;
    out_->detail_flags =
      ZLINK_MONITOR_SNAPSHOT_DETAIL_READY_PEER_COUNT
      | ZLINK_MONITOR_SNAPSHOT_DETAIL_SND_PENDING_MSGS
      | ZLINK_MONITOR_SNAPSHOT_DETAIL_RCV_PENDING_MSGS;
    out_->ready_peer_count = static_cast<uint32_t> (_pipes.size ());
    if (out_->ready_peer_count > 0)
        out_->state_flags |= ZLINK_MONITOR_STATE_READY;

    for (pipes_t::size_type i = 0; i < _pipes.size (); ++i) {
        pipe_t *pipe = _pipes[i];
        out_->snd_pending_msgs += pipe->get_snd_pending_msgs ();
        out_->rcv_pending_msgs += pipe->get_rcv_pending_msgs_approx ();
    }

    return 0;
}

void zlink::socket_base_t::socket_peer_remote_endpoints (
  std::vector<std::string> *out_)
{
    if (!out_)
        return;

    process_commands (0, false);
    out_->clear ();
    out_->reserve (_pipes.size ());
    for (pipes_t::size_type i = 0; i < _pipes.size (); ++i) {
        pipe_t *pipe = _pipes[i];
        const std::string &remote = pipe->get_endpoint_pair ().remote;
        if (!remote.empty ())
            out_->push_back (remote);
    }
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

// TODO consider renaming protocol_ to scheme_ in conformance with RFC 3986
// terminology, but this requires extensive changes to be consistent
int zlink::socket_base_t::parse_uri (const char *uri_,
                                   std::string &protocol_,
                                   std::string &path_)
{
    zlink_assert (uri_ != NULL);

    const std::string uri (uri_);
    const std::string::size_type pos = uri.find ("://");
    if (pos == std::string::npos) {
        errno = EINVAL;
        return -1;
    }
    protocol_ = uri.substr (0, pos);
    path_ = uri.substr (pos + 3);

    if (protocol_.empty () || path_.empty ()) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int zlink::socket_base_t::check_protocol (const std::string &protocol_) const
{
    //  First check out whether the protocol is something we are aware of.
    if (protocol_ != protocol_name::inproc
#if defined ZLINK_HAVE_IPC
        && protocol_ != protocol_name::ipc
#endif
        && protocol_ != protocol_name::tcp
#ifdef ZLINK_HAVE_WS
        && protocol_ != protocol_name::ws
#endif
#ifdef ZLINK_HAVE_WSS
        && protocol_ != protocol_name::wss
#endif
#ifdef ZLINK_HAVE_TLS
        && protocol_ != protocol_name::tls
#endif
#ifdef ZLINK_HAVE_OPENPGM
        && protocol_ != protocol_name::pgm
        && protocol_ != protocol_name::epgm
#endif
    ) {
        errno = EPROTONOSUPPORT;
        return -1;
    }

#ifdef ZLINK_HAVE_OPENPGM
    //  PGM/EPGM is temporarily disabled for PUB/SUB family in zlink.
    if (protocol_ == protocol_name::pgm || protocol_ == protocol_name::epgm) {
        errno = EPROTONOSUPPORT;
        return -1;
    }
#endif

    //  Protocol is available.
    return 0;
}

void zlink::socket_base_t::attach_pipe (pipe_t *pipe_,
                                      bool subscribe_to_all_,
                                      bool locally_initiated_)
{
    if (getenv ("ZLINK_DEBUG_SOCKET_TERM")) {
        fprintf (stderr,
                 "[socket-term] attach socket=%p id=%d type=%d pipe=%p peer=%p local=%d\n",
                 static_cast<void *> (this), options.socket_id, options.type,
                 static_cast<void *> (pipe_),
                 static_cast<void *> (pipe_ ? pipe_->get_peer () : NULL),
                 locally_initiated_ ? 1 : 0);
        fflush (stderr);
    }

    //  First, register the pipe so that we can terminate it later on.
    pipe_->set_event_sink (this);
    _pipes.push_back (pipe_);

    //  Let the derived socket type know about new pipe.
    xattach_pipe (pipe_, subscribe_to_all_, locally_initiated_);

    //  If the socket is already being closed, ask any new pipes to terminate
    //  straight away.
    if (is_terminating ()) {
        register_term_acks (1);
#ifndef NDEBUG
        ++_term_pipe_acks_registered;
#endif
        pipe_->terminate (false);
    }
}

int zlink::socket_base_t::setsockopt (int option_,
                                    const void *optval_,
                                    size_t optvallen_)
{
    if (!enter_public_api ())
        return -1;

    if (unlikely (_ctx_terminated)) {
        leave_public_api ();
        errno = ETERM;
        return -1;
    }

    //  First, check whether specific socket type overloads the option.
    lock_public_api_sync ();
    int rc = xsetsockopt (option_, optval_, optvallen_);
    unlock_public_api_sync ();
    if (rc == 0 || errno != EINVAL) {
        leave_public_api ();
        return rc;
    }

    //  If the socket type doesn't support the option, pass it to
    //  the generic option parser.
    lock_public_api_sync ();
    rc = options.setsockopt (option_, optval_, optvallen_);
    update_pipe_options (option_);
    unlock_public_api_sync ();

    leave_public_api ();
    return rc;
}

int zlink::socket_base_t::getsockopt (int option_,
                                    void *optval_,
                                    size_t *optvallen_)
{
    if (!enter_public_api ())
        return -1;

    if (unlikely (_ctx_terminated)) {
        leave_public_api ();
        errno = ETERM;
        return -1;
    }

    //  First, check whether specific socket type overloads the option.
    lock_public_api_sync ();
    int rc = xgetsockopt (option_, optval_, optvallen_);
    unlock_public_api_sync ();
    if (rc == 0 || errno != EINVAL) {
        leave_public_api ();
        return rc;
    }

    if (option_ == ZLINK_RCVMORE) {
        rc = do_getsockopt<int> (optval_, optvallen_, _rcvmore ? 1 : 0);
        leave_public_api ();
        return rc;
    }

    if (option_ == ZLINK_SOCKOPT_FD) {
        rc = do_getsockopt<fd_t> (
          optval_, optvallen_, static_cast<mailbox_t *> (_mailbox)->get_fd ());
        leave_public_api ();
        return rc;
    }

    if (option_ == ZLINK_SOCKOPT_EVENTS) {
        lock_public_api_sync ();
        const int rc = process_commands (0, false);
        if (rc != 0 && (errno == EINTR || errno == ETERM)) {
            unlock_public_api_sync ();
            leave_public_api ();
            return -1;
        }
        errno_assert (rc == 0);
        const int out_rc = do_getsockopt<int> (optval_, optvallen_,
                                               has_out () ? ZLINK_POLLOUT : 0);
        unlock_public_api_sync ();
        leave_public_api ();
        return out_rc;
    }

    if (option_ == ZLINK_SOCKOPT_LAST_ENDPOINT) {
        lock_public_api_sync ();
        const int out_rc = do_getsockopt (optval_, optvallen_, _last_endpoint);
        unlock_public_api_sync ();
        leave_public_api ();
        return out_rc;
    }

    lock_public_api_sync ();
    rc = options.getsockopt (option_, optval_, optvallen_);
    unlock_public_api_sync ();
    leave_public_api ();
    return rc;
}

int zlink::socket_base_t::get_events (int events_, uint32_t *out_)
{
    if (!enter_public_api ())
        return -1;

    if (!out_) {
        leave_public_api ();
        errno = EINVAL;
        return -1;
    }

    lock_public_api_sync ();
    const int rc = process_commands (0, false);
    if (rc != 0 && (errno == EINTR || errno == ETERM)) {
        unlock_public_api_sync ();
        leave_public_api ();
        return -1;
    }
    errno_assert (rc == 0);

    uint32_t events = 0;
    if (events_ & ZLINK_POLLOUT) {
        if (has_out ())
            events |= ZLINK_POLLOUT;
    }

    *out_ = events;
    unlock_public_api_sync ();
    leave_public_api ();
    return 0;
}

int zlink::socket_base_t::get_events_internal (int events_, uint32_t *out_)
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }

    const int rc = process_commands (0, false);
    if (rc != 0 && (errno == EINTR || errno == ETERM)) {
        return -1;
    }
    errno_assert (rc == 0);

    uint32_t events = 0;
    if ((events_ & ZLINK_POLLIN) && has_in ())
        events |= ZLINK_POLLIN;
    if ((events_ & ZLINK_POLLOUT) && has_out ())
        events |= ZLINK_POLLOUT;

    *out_ = events;
    return 0;
}

int zlink::socket_base_t::join (const char *group_)
{
    return xjoin (group_);
}

int zlink::socket_base_t::leave (const char *group_)
{
    return xleave (group_);
}

int zlink::socket_base_t::bind (const char *endpoint_uri_)
{

    if (unlikely (_ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    //  Process pending commands, if any.
    int rc = process_commands (0, false);
    if (unlikely (rc != 0)) {
        return -1;
    }

    //  Parse endpoint_uri_ string.
    std::string protocol;
    std::string address;
    if (parse_uri (endpoint_uri_, protocol, address)
        || check_protocol (protocol)) {
        return -1;
    }

    if (protocol == protocol_name::inproc) {
        const endpoint_t endpoint = {this, options};
        rc = register_endpoint (endpoint_uri_, endpoint);
        if (rc == 0) {
            connect_pending (endpoint_uri_, this);
            _last_endpoint.assign (endpoint_uri_);
            options.connected = true;
        }
        return rc;
    }

#ifdef ZLINK_HAVE_OPENPGM
    if (protocol == protocol_name::pgm || protocol == protocol_name::epgm) {
        //  For convenience's sake, bind can be used interchangeable with
        //  connect for PGM/EPGM transports.
        rc = connect (endpoint_uri_);
        if (rc != -1)
            options.connected = true;
        return rc;
    }
#endif

    //  Remaining transports require to be run in an I/O thread, so at this
    //  point we'll choose one.
    io_thread_t *io_thread = choose_io_thread (options.affinity);
    if (!io_thread) {
        errno = EMTHREAD;
        return -1;
    }

    if (protocol == protocol_name::tcp) {
        //  Use ASIO-based listener for async_accept
        asio_tcp_listener_t *listener =
          new (std::nothrow) asio_tcp_listener_t (io_thread, this, options);
        alloc_assert (listener);
        rc = listener->set_local_address (address.c_str ());
        if (rc != 0) {
            LIBZLINK_DELETE (listener);
            event_bind_failed (make_unconnected_bind_endpoint_pair (address),
                               zlink_errno ());
            return -1;
        }

        // Save last endpoint URI
        listener->get_local_address (_last_endpoint);

        add_endpoint (make_unconnected_bind_endpoint_pair (_last_endpoint),
                      static_cast<own_t *> (listener), NULL);
        options.connected = true;
        return 0;
    }

#if defined ZLINK_HAVE_TLS && defined ZLINK_HAVE_ASIO_SSL
    if (protocol == protocol_name::tls) {
        //  ASIO-only: Use ASIO-based TLS listener with SSL/TLS encryption
        asio_tls_listener_t *listener =
          new (std::nothrow) asio_tls_listener_t (io_thread, this, options);
        alloc_assert (listener);
        rc = listener->set_local_address (address.c_str ());
        if (rc != 0) {
            LIBZLINK_DELETE (listener);
            event_bind_failed (make_unconnected_bind_endpoint_pair (address),
                               zlink_errno ());
            return -1;
        }

        // Save last endpoint URI
        listener->get_local_address (_last_endpoint);

        add_endpoint (make_unconnected_bind_endpoint_pair (_last_endpoint),
                      static_cast<own_t *> (listener), NULL);
        options.connected = true;
        return 0;
    }
#endif

#if defined ZLINK_HAVE_IPC
    if (protocol == protocol_name::ipc) {
        asio_ipc_listener_t *listener =
          new (std::nothrow) asio_ipc_listener_t (io_thread, this, options);
        alloc_assert (listener);
        int rc = listener->set_local_address (address.c_str ());
        if (rc != 0) {
            LIBZLINK_DELETE (listener);
            event_bind_failed (make_unconnected_bind_endpoint_pair (address),
                               zlink_errno ());
            return -1;
        }

        // Save last endpoint URI
        listener->get_local_address (_last_endpoint);

        add_endpoint (make_unconnected_bind_endpoint_pair (_last_endpoint),
                      static_cast<own_t *> (listener), NULL);
        options.connected = true;
        return 0;
    }
#endif

#if defined ZLINK_HAVE_WS
    if (protocol == protocol_name::ws
#if defined ZLINK_HAVE_WSS
        || protocol == protocol_name::wss
#endif
    ) {
        const bool secure =
#if defined ZLINK_HAVE_WSS
          protocol == protocol_name::wss;
#else
          false;
#endif

        //  Resolve WebSocket address
        ws_address_t *ws_addr =
#if defined ZLINK_HAVE_WSS
          secure ? static_cast<ws_address_t *> (new (std::nothrow) wss_address_t ())
                 :
#endif
                 new (std::nothrow) ws_address_t ();
        alloc_assert (ws_addr);
        rc = ws_addr->resolve (address.c_str (), true, options.ipv6);
        if (rc != 0) {
            LIBZLINK_DELETE (ws_addr);
            event_bind_failed (make_unconnected_bind_endpoint_pair (address),
                               zlink_errno ());
            return -1;
        }

        //  ASIO-only: Use ASIO-based WebSocket listener
        asio_ws_listener_t *listener =
          new (std::nothrow) asio_ws_listener_t (io_thread, this, options);
        alloc_assert (listener);
        rc = listener->set_local_address (ws_addr, secure);
        LIBZLINK_DELETE (ws_addr);
        if (rc != 0) {
            LIBZLINK_DELETE (listener);
            event_bind_failed (make_unconnected_bind_endpoint_pair (address),
                               zlink_errno ());
            return -1;
        }

        //  Save last endpoint URI
        listener->get_local_address (_last_endpoint);

        add_endpoint (make_unconnected_bind_endpoint_pair (_last_endpoint),
                      static_cast<own_t *> (listener), NULL);
        options.connected = true;
        return 0;
    }
#endif

    zlink_assert (false);
    return -1;
}

int zlink::socket_base_t::connect (const char *endpoint_uri_)
{
    return connect_internal (endpoint_uri_);
}

int zlink::socket_base_t::connect_internal (const char *endpoint_uri_)
{
    if (unlikely (_ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    //  Process pending commands, if any.
    int rc = process_commands (0, false);
    if (unlikely (rc != 0)) {
        return -1;
    }

    //  Parse endpoint_uri_ string.
    std::string protocol;
    std::string address;
    if (parse_uri (endpoint_uri_, protocol, address)
        || check_protocol (protocol)) {
        return -1;
    }

    // STREAM is server-side only; outbound connect is intentionally disabled.
    if (options.type == ZLINK_STREAM) {
        errno = EOPNOTSUPP;
        return -1;
    }

    if (protocol == protocol_name::inproc) {
        //  TODO: inproc connect is specific with respect to creating pipes
        //  as there's no 'reconnect' functionality implemented. Once that
        //  is in place we should follow generic pipe creation algorithm.

        //  Find the peer endpoint.
        const endpoint_t peer = find_endpoint (endpoint_uri_);

        // The total HWM for an inproc connection should be the sum of
        // the binder's HWM and the connector's HWM.
        const int sndhwm = peer.socket == NULL ? options.sndhwm
                           : options.sndhwm != 0 && peer.options.rcvhwm != 0
                             ? options.sndhwm + peer.options.rcvhwm
                             : 0;
        const int rcvhwm = peer.socket == NULL ? options.rcvhwm
                           : options.rcvhwm != 0 && peer.options.sndhwm != 0
                             ? options.rcvhwm + peer.options.sndhwm
                             : 0;

        //  Create a bi-directional pipe to connect the peers.
        object_t *parents[2] = {this, peer.socket == NULL ? this : peer.socket};
        pipe_t *new_pipes[2] = {NULL, NULL};

        const bool conflate = get_effective_conflate_option (options);

        int hwms[2] = {conflate ? -1 : sndhwm, conflate ? -1 : rcvhwm};
        bool conflates[2] = {conflate, conflate};
        rc = pipepair (parents, new_pipes, hwms, conflates);
        if (!conflate) {
            new_pipes[0]->set_hwms_boost (peer.options.sndhwm,
                                          peer.options.rcvhwm);
            new_pipes[1]->set_hwms_boost (options.sndhwm, options.rcvhwm);
        }

        errno_assert (rc == 0);

        bool connected_inproc_now = false;

        if (!peer.socket) {
            //  The peer doesn't exist yet so we don't know whether
            //  to send the routing id message or not. To resolve this,
            //  we always send our routing id and drop it later if
            //  the peer doesn't expect it.
            send_routing_id (new_pipes[0], options);

            const endpoint_t endpoint = {this, options};
            connected_inproc_now =
              pend_connection (std::string (endpoint_uri_), endpoint, new_pipes);
        } else {
            //  If required, send the routing id of the local socket to the peer.
            if (peer.options.recv_routing_id) {
                send_routing_id (new_pipes[0], options);
            }

            //  If required, send the routing id of the peer to the local socket.
            if (options.recv_routing_id) {
                send_routing_id (new_pipes[1], peer.options);
            }

            new_pipes[0]->set_peer_routing_id (peer.options.routing_id,
                                               peer.options.routing_id_size);
            new_pipes[1]->set_peer_routing_id (options.routing_id,
                                               options.routing_id_size);

            //  Attach remote end of the pipe to the peer socket. Note that peer's
            //  seqnum was incremented in find_endpoint function. We don't need it
            //  increased here.
            send_bind (peer.socket, new_pipes[1], false);
            peer.socket->emit_inproc_connection_ready (new_pipes[1]);
            connected_inproc_now = true;
        }

        //  Attach local end of the pipe to this socket object.
        attach_pipe (new_pipes[0], false, true);
        if (connected_inproc_now)
            emit_inproc_connection_ready (new_pipes[0]);

        // Save last endpoint URI
        _last_endpoint.assign (endpoint_uri_);

        // remember inproc connections for disconnect
        _inprocs.emplace (endpoint_uri_, new_pipes[0]);

        options.connected = true;
        return 0;
    }
    if (unlikely (0 != _endpoints.count (endpoint_uri_))) {
        // Treat duplicate connects as no-ops for all socket types.
        return 0;
    }

    //  Choose the I/O thread to run the session in.
    io_thread_t *io_thread = choose_io_thread (options.affinity);
    if (!io_thread) {
        errno = EMTHREAD;
        return -1;
    }

    address_t *paddr =
      new (std::nothrow) address_t (protocol, address, this->get_ctx ());
    alloc_assert (paddr);

    //  Resolve address (if needed by the protocol)
    if (protocol == protocol_name::tcp
#ifdef ZLINK_HAVE_TLS
        || protocol == protocol_name::tls  // TLS uses TCP address format
#endif
    ) {
        //  Do some basic sanity checks on tcp:// address syntax
        //  - hostname starts with digit or letter, with embedded '-' or '.'
        //  - IPv6 address may contain hex chars and colons.
        //  - IPv6 link local address may contain % followed by interface name / zone_id
        //    (Reference: https://tools.ietf.org/html/rfc4007)
        //  - IPv4 address may contain decimal digits and dots.
        //  - Address must end in ":port" where port is *, or numeric
        //  - Address may contain two parts separated by ':'
        //  Following code is quick and dirty check to catch obvious errors,
        //  without trying to be fully accurate.
        const char *check = address.c_str ();
        if (isalnum (*check) || isxdigit (*check) || *check == '['
            || *check == ':') {
            check++;
            while (isalnum (*check) || isxdigit (*check) || *check == '.'
                   || *check == '-' || *check == ':' || *check == '%'
                   || *check == ';' || *check == '[' || *check == ']'
                   || *check == '_' || *check == '*') {
                check++;
            }
        }
        //  Assume the worst, now look for success
        rc = -1;
        //  Did we reach the end of the address safely?
        if (*check == 0) {
            //  Do we have a valid port string? (cannot be '*' in connect
            check = strrchr (address.c_str (), ':');
            if (check) {
                check++;
                if (*check && (isdigit (*check)))
                    rc = 0; //  Valid
            }
        }
        if (rc == -1) {
            errno = EINVAL;
            LIBZLINK_DELETE (paddr);
            return -1;
        }
        //  Defer resolution until a socket is opened
        paddr->resolved.tcp_addr = NULL;
    }
#ifdef ZLINK_HAVE_WS
#ifdef ZLINK_HAVE_WSS
    else if (protocol == protocol_name::ws || protocol == protocol_name::wss) {
        if (protocol == protocol_name::wss) {
            paddr->resolved.wss_addr = new (std::nothrow) wss_address_t ();
            alloc_assert (paddr->resolved.wss_addr);
            rc = paddr->resolved.wss_addr->resolve (address.c_str (), false,
                                                    options.ipv6);
        } else
#else
    else if (protocol == protocol_name::ws) {
#endif
        {
            paddr->resolved.ws_addr = new (std::nothrow) ws_address_t ();
            alloc_assert (paddr->resolved.ws_addr);
            rc = paddr->resolved.ws_addr->resolve (address.c_str (), false,
                                                   options.ipv6);
        }

        if (rc != 0) {
            LIBZLINK_DELETE (paddr);
            return -1;
        }
    }
#endif

#if defined ZLINK_HAVE_IPC
    else if (protocol == protocol_name::ipc) {
        paddr->resolved.ipc_addr = new (std::nothrow) ipc_address_t ();
        alloc_assert (paddr->resolved.ipc_addr);
        int rc = paddr->resolved.ipc_addr->resolve (address.c_str ());
        if (rc != 0) {
            LIBZLINK_DELETE (paddr);
            return -1;
        }
    }
#endif

#ifdef ZLINK_HAVE_OPENPGM
    if (protocol == protocol_name::pgm || protocol == protocol_name::epgm) {
        struct pgm_addrinfo_t *res = NULL;
        uint16_t port_number = 0;
        int rc =
          pgm_socket_t::init_address (address.c_str (), &res, &port_number);
        if (res != NULL)
            pgm_freeaddrinfo (res);
        if (rc != 0 || port_number == 0) {
            LIBZLINK_DELETE (paddr);
            return -1;
        }
    }
#endif

    //  Create session.
    session_base_t *session =
      session_base_t::create (io_thread, true, this, options, paddr);
    errno_assert (session);

    //  PGM does not support subscription forwarding; ask for all data.
#ifdef ZLINK_HAVE_OPENPGM
    const bool subscribe_to_all = protocol == protocol_name::pgm
                                  || protocol == protocol_name::epgm;
#else
    const bool subscribe_to_all = false;
#endif
    pipe_t *newpipe = NULL;

    if (options.immediate != 1 || subscribe_to_all) {
        //  Create a bi-directional pipe.
        object_t *parents[2] = {this, session};
        pipe_t *new_pipes[2] = {NULL, NULL};

        const bool conflate = get_effective_conflate_option (options);

        int hwms[2] = {conflate ? -1 : options.sndhwm,
                       conflate ? -1 : options.rcvhwm};
        bool conflates[2] = {conflate, conflate};
        rc = pipepair (parents, new_pipes, hwms, conflates);
        errno_assert (rc == 0);

        //  Attach local end of the pipe to the socket object.
        attach_pipe (new_pipes[0], subscribe_to_all, true);
        newpipe = new_pipes[0];

        //  Attach remote end of the pipe to the session object later on.
        session->attach_pipe (new_pipes[1]);
    }

    //  Save last endpoint URI
    paddr->to_string (_last_endpoint);

    add_endpoint (make_unconnected_connect_endpoint_pair (endpoint_uri_),
                  static_cast<own_t *> (session), newpipe);
    return 0;
}

std::string
zlink::socket_base_t::resolve_tcp_addr (std::string endpoint_uri_pair_,
                                      const char *tcp_address_)
{
    // The resolved last_endpoint is used as a key in the endpoints map.
    // The address passed by the user might not match in the TCP case due to
    // IPv4-in-IPv6 mapping (EG: tcp://[::ffff:127.0.0.1]:9999), so try to
    // resolve before giving up. Given at this stage we don't know whether a
    // socket is connected or bound, try with both.
    if (_endpoints.find (endpoint_uri_pair_) == _endpoints.end ()) {
        tcp_address_t *tcp_addr = new (std::nothrow) tcp_address_t ();
        alloc_assert (tcp_addr);
        int rc = tcp_addr->resolve (tcp_address_, false, options.ipv6);

        if (rc == 0) {
            tcp_addr->to_string (endpoint_uri_pair_);
            if (_endpoints.find (endpoint_uri_pair_) == _endpoints.end ()) {
                rc = tcp_addr->resolve (tcp_address_, true, options.ipv6);
                if (rc == 0) {
                    tcp_addr->to_string (endpoint_uri_pair_);
                }
            }
        }
        LIBZLINK_DELETE (tcp_addr);
    }
    return endpoint_uri_pair_;
}

void zlink::socket_base_t::add_endpoint (
  const endpoint_uri_pair_t &endpoint_pair_, own_t *endpoint_, pipe_t *pipe_)
{
    //  Activate the session. Make it a child of this socket.
    launch_child (endpoint_);
    _endpoints.ZLINK_MAP_INSERT_OR_EMPLACE (endpoint_pair_.identifier (),
                                          endpoint_pipe_t (endpoint_, pipe_));

    if (pipe_ != NULL)
        pipe_->set_endpoint_pair (endpoint_pair_);
}

int zlink::socket_base_t::term_endpoint (const char *endpoint_uri_)
{
    if (!enter_public_api ())
        return -1;

    //  Check whether the context hasn't been shut down yet.
    if (unlikely (_ctx_terminated)) {
        errno = ETERM;
        leave_public_api ();
        return -1;
    }

    //  Check whether endpoint address passed to the function is valid.
    if (unlikely (!endpoint_uri_)) {
        errno = EINVAL;
        leave_public_api ();
        return -1;
    }

    //  Process pending commands, if any, since there could be pending unprocessed process_own()'s
    //  (from launch_child() for example) we're asked to terminate now.
    const int rc = process_commands (0, false);
    if (unlikely (rc != 0)) {
        leave_public_api ();
        return -1;
    }

    //  Parse endpoint_uri_ string.
    std::string uri_protocol;
    std::string uri_path;
    if (parse_uri (endpoint_uri_, uri_protocol, uri_path)
        || check_protocol (uri_protocol)) {
        leave_public_api ();
        return -1;
    }

    const std::string endpoint_uri_str = std::string (endpoint_uri_);

    // Disconnect an inproc socket
    if (uri_protocol == protocol_name::inproc) {
        const int inproc_rc = unregister_endpoint (endpoint_uri_str, this) == 0
                                ? 0
                                : _inprocs.erase_pipes (endpoint_uri_str);
        leave_public_api ();
        return inproc_rc;
    }

    const std::string resolved_endpoint_uri =
      (uri_protocol == protocol_name::tcp
#ifdef ZLINK_HAVE_TLS
       || uri_protocol == protocol_name::tls  // TLS uses TCP address format
#endif
      )
        ? resolve_tcp_addr (endpoint_uri_str, uri_path.c_str ())
        : endpoint_uri_str;

    //  Find the endpoints range (if any) corresponding to the endpoint_uri_pair_ string.
    const std::pair<endpoints_t::iterator, endpoints_t::iterator> range =
      _endpoints.equal_range (resolved_endpoint_uri);
    if (range.first == range.second) {
        errno = ENOENT;
        leave_public_api ();
        return -1;
    }

    for (endpoints_t::iterator it = range.first; it != range.second; ++it) {
        //  If we have an associated pipe, terminate it.
        if (it->second.second != NULL)
            it->second.second->terminate (false);
        term_child (it->second.first);
    }

    //  Accepted pipes are not tracked in _endpoints; terminate any active pipe
    //  that belongs to this endpoint identifier.
    for (pipes_t::size_type i = 0; i < _pipes.size (); ++i) {
        pipe_t *const pipe = _pipes[i];
        if (!pipe)
            continue;
        if (pipe->get_endpoint_pair ().identifier () == resolved_endpoint_uri)
            pipe->terminate (false);
    }
    _endpoints.erase (range.first, range.second);
    leave_public_api ();
    return 0;
}

int zlink::socket_base_t::send (msg_t *msg_, int flags_)
{
    if (!enter_public_api ())
        return -1;

    //  Check whether the context hasn't been shut down yet.
    if (unlikely (_ctx_terminated)) {
        leave_public_api ();
        errno = ETERM;
        return -1;
    }

    //  Check whether message passed to the function is valid.
    if (unlikely (!msg_ || !msg_->check ())) {
        leave_public_api ();
        errno = EFAULT;
        return -1;
    }

    //  Process pending commands, if any.
    int rc = process_commands (0, true);
    if (unlikely (rc != 0)) {
        leave_public_api ();
        return -1;
    }

    //  Clear any user-visible flags that are set on the message.
    msg_->reset_flags (msg_t::more);

    //  At this point we impose the flags on the message.
    if (flags_ & ZLINK_SNDMORE)
        msg_->set_flags (msg_t::more);

    msg_->reset_metadata ();

    //  Try to send the message using method in each socket class
    {
        lock_public_api_sync ();
        rc = xsend (msg_);
        unlock_public_api_sync ();
    }
    if (rc == 0) {
        leave_public_api ();
        return 0;
    }
    //  Special case: -2 means pipe is dead while a
    //  multi-part send is in progress and can't be recovered, so drop
    //  silently when in blocking mode to keep backward compatibility.
    if (unlikely (rc == -2)) {
        if (!((flags_ & ZLINK_DONTWAIT) || options.sndtimeo == 0)) {
            rc = msg_->close ();
            errno_assert (rc == 0);
            rc = msg_->init ();
            errno_assert (rc == 0);
            leave_public_api ();
            return 0;
        }
    }
    if (unlikely (errno != EAGAIN)) {
        if ((flags_ & ZLINK_DONTWAIT) || options.sndtimeo == 0) {
            if (errno == ENOTCONN || errno == EHOSTUNREACH
                || errno == ETIMEDOUT) {
                arm_send_ready_notification ();
            }
        }
        leave_public_api ();
        return -1;
    }

    //  In case of non-blocking send we'll simply propagate
    //  the error - including EAGAIN - up the stack.
    if ((flags_ & ZLINK_DONTWAIT) || options.sndtimeo == 0) {
        arm_send_ready_notification ();
        leave_public_api ();
        return -1;
    }

    //  Compute the time when the timeout should occur.
    //  If the timeout is infinite, don't care.
    int timeout = options.sndtimeo;
    const uint64_t end = timeout < 0 ? 0 : (_clock.now_ms () + timeout);

    //  Oops, we couldn't send the message. Wait for the next
    //  command, process it and try to send the message again.
    //  If timeout is reached in the meantime, return EAGAIN.
    while (true) {
        rc = process_commands (timeout, false);
        if (unlikely (rc != 0)) {
            leave_public_api ();
            return -1;
        }
        {
            lock_public_api_sync ();
            rc = xsend (msg_);
            unlock_public_api_sync ();
        }
        if (rc == 0) {
            break;
        }
        if (unlikely (errno != EAGAIN)) {
            leave_public_api ();
            return -1;
        }
        if (timeout > 0) {
            timeout = static_cast<int> (end - _clock.now_ms ());
            if (timeout <= 0) {
                errno = EAGAIN;
                leave_public_api ();
                return -1;
            }
        }
    }

    leave_public_api ();
    return 0;
}

int zlink::socket_base_t::recv (msg_t *msg_, int flags_)
{

    //  Check whether the context hasn't been shut down yet.
    if (unlikely (_ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    //  Check whether message passed to the function is valid.
    if (unlikely (!msg_ || !msg_->check ())) {
        errno = EFAULT;
        return -1;
    }

    //  Once every inbound_poll_rate messages check for signals and process
    //  incoming commands. This happens only if we are not polling altogether
    //  because there are messages available all the time. If poll occurs,
    //  ticks is set to zero and thus we avoid this code.
    //
    //  Note that 'recv' uses different command throttling algorithm (the one
    //  described above) from the one used by 'send'. This is because counting
    //  ticks is more efficient than doing RDTSC all the time.
    if (++_ticks == inbound_poll_rate) {
        if (unlikely (process_commands (0, false) != 0)) {
            return -1;
        }
        _ticks = 0;
    }

    //  Get the message.
    int rc = xrecv (msg_);
    if (unlikely (rc != 0 && errno != EAGAIN)) {
        return -1;
    }

    //  If we have the message, return immediately.
    if (rc == 0) {
        extract_flags (msg_);
        return 0;
    }

    //  If the message cannot be fetched immediately, there are two scenarios.
    //  For non-blocking recv, commands are processed in case there's an
    //  activate_reader command already waiting in a command pipe.
    //  If it's not, return EAGAIN.
    if ((flags_ & ZLINK_DONTWAIT) || options.rcvtimeo == 0) {
        if (unlikely (process_commands (0, false) != 0)) {
            return -1;
        }
        _ticks = 0;

        rc = xrecv (msg_);
        if (rc < 0) {
            return rc;
        }
        extract_flags (msg_);

        return 0;
    }

    //  Compute the time when the timeout should occur.
    //  If the timeout is infinite, don't care.
    int timeout = options.rcvtimeo;
    const uint64_t end = timeout < 0 ? 0 : (_clock.now_ms () + timeout);

    //  In blocking scenario, commands are processed over and over again until
    //  we are able to fetch a message.
    bool block = (_ticks != 0);
    while (true) {
        if (unlikely (process_commands (block ? timeout : 0, false) != 0)) {
            return -1;
        }
        rc = xrecv (msg_);
        if (rc == 0) {
            _ticks = 0;
            break;
        }
        if (unlikely (errno != EAGAIN)) {
            return -1;
        }
        block = true;
        if (timeout > 0) {
            timeout = static_cast<int> (end - _clock.now_ms ());
            if (timeout <= 0) {
                errno = EAGAIN;
                return -1;
            }
        }
    }

    extract_flags (msg_);
    return 0;
}

int zlink::socket_base_t::stream_dispatch_msg_from_io (msg_t *msg_,
                                                       pipe_t *pipe_)
{
    return xstream_dispatch_msg (msg_, pipe_);
}

int zlink::socket_base_t::socket_msg_dispatch_from_io (msg_t *msg_,
                                                       pipe_t *pipe_)
{
    if (!socket_msg_dispatch_active ())
        return 0;
    std::lock_guard<std::recursive_mutex> dispatch_lock (
      _socket_msg_dispatch_sync);
    pipe_t *previous_pipe = g_current_socket_msg_dispatch_pipe;
    g_current_socket_msg_dispatch_pipe = pipe_;
    const int rc = xsocket_msg_dispatch (msg_, pipe_);
    g_current_socket_msg_dispatch_pipe = previous_pipe;
    return rc;
}

int zlink::socket_base_t::socket_set_msg_handler (
  zlink_socket_msg_handler_fn handler_)
{
    return socket_set_msg_handler_ex (handler_, NULL);
}

int zlink::socket_base_t::socket_set_msg_handler_ex (
  zlink_socket_msg_handler_fn handler_, void *subject_)
{
    if (!enter_public_api ())
        return -1;
    if (!handler_) {
        leave_public_api ();
        errno = EINVAL;
        return -1;
    }

    _socket_msg_handler.store (handler_, std::memory_order_release);
    _socket_msg_handler_subject.store (subject_, std::memory_order_release);
    leave_public_api ();
    return 0;
}

int zlink::socket_base_t::socket_set_spot_handler (
  zlink_spot_handler_fn handler_)
{
    if (!enter_public_api ())
        return -1;
    if (!handler_) {
        leave_public_api ();
        errno = EINVAL;
        return -1;
    }

    _spot_handler.store (handler_, std::memory_order_release);
    if (sub_dispatch_active ()) {
        leave_public_api ();
        return 0;
    }

    const int rc =
      sub_dispatch_start (&socket_base_t::dispatch_spot_handler_from_io, this);
    leave_public_api ();
    return rc;
}

int zlink::socket_base_t::socket_set_xpub_handler (
  zlink_xpub_handler_fn handler_)
{
    if (!enter_public_api ())
        return -1;
    if (!handler_) {
        leave_public_api ();
        errno = EINVAL;
        return -1;
    }

    _xpub_handler.store (handler_, std::memory_order_release);
    if (xpub_dispatch_active ()) {
        leave_public_api ();
        return 0;
    }

    const int rc = xpub_dispatch_start ();
    leave_public_api ();
    return rc;
}

int zlink::socket_base_t::socket_set_send_ready_handler (
  zlink_send_ready_handler_fn handler_)
{
    return socket_set_send_ready_handler_ex (handler_, NULL);
}

int zlink::socket_base_t::socket_set_send_ready_handler_ex (
  zlink_send_ready_handler_fn handler_, void *subject_)
{
    if (!enter_public_api ())
        return -1;
    if (!handler_) {
        leave_public_api ();
        errno = EINVAL;
        return -1;
    }
    if (send_ready_dispatch_in_callback ()) {
        leave_public_api ();
        errno = EDEADLK;
        return -1;
    }

    scoped_lock_t writer_lock (_send_ready_writer_sync);
    _send_ready_seq.fetch_add (1, std::memory_order_acq_rel);
    _send_ready_handler.store (handler_, std::memory_order_release);
    _send_ready_handler_subject.store (subject_, std::memory_order_release);
    _send_ready_seq.fetch_add (1, std::memory_order_acq_rel);
    leave_public_api ();
    return 0;
}

bool zlink::socket_base_t::socket_msg_dispatch_active () const
{
    return _socket_msg_handler.load (std::memory_order_acquire) != NULL;
}

zlink::socket_base_t *
zlink::socket_base_t::current_socket_msg_dispatch_socket ()
{
    return g_current_socket_msg_dispatch_socket;
}

zlink::socket_base_t *
zlink::socket_base_t::current_send_ready_dispatch_socket ()
{
    return g_current_send_ready_dispatch_socket;
}

zlink::pipe_t *zlink::socket_base_t::current_socket_msg_dispatch_pipe ()
{
    return g_current_socket_msg_dispatch_pipe;
}

void *zlink::socket_base_t::current_socket_msg_dispatch_subject ()
{
    return g_current_socket_msg_dispatch_subject;
}

bool zlink::socket_base_t::current_socket_msg_dispatch_source_rid (
  zlink_routing_id_t *out_)
{
    if (!out_ || !g_current_socket_msg_dispatch_source_rid_valid)
        return false;
    *out_ = g_current_socket_msg_dispatch_source_rid;
    return true;
}

bool zlink::socket_base_t::send_ready_dispatch_in_callback () const
{
    return g_current_send_ready_dispatch_socket == this;
}

void zlink::socket_base_t::invoke_send_ready_handler_for_testing ()
{
    zlink_send_ready_handler_fn handler = NULL;
    void *subject = NULL;
    if (!send_ready_slot (&handler, &subject))
        return;

    if (!enter_callback_api ())
        return;

    socket_base_t *previous = g_current_send_ready_dispatch_socket;
    g_current_send_ready_dispatch_socket = this;
    handler (subject ? subject : this);
    g_current_send_ready_dispatch_socket = previous;
    leave_callback_api ();
}

zlink_socket_msg_handler_fn zlink::socket_base_t::socket_msg_handler () const
{
    return _socket_msg_handler.load (std::memory_order_acquire);
}

zlink_spot_handler_fn zlink::socket_base_t::socket_spot_handler () const
{
    return _spot_handler.load (std::memory_order_acquire);
}

zlink_xpub_handler_fn zlink::socket_base_t::socket_xpub_handler () const
{
    return _xpub_handler.load (std::memory_order_acquire);
}

zlink_send_ready_handler_fn
zlink::socket_base_t::socket_send_ready_handler () const
{
    zlink_send_ready_handler_fn handler = NULL;
    void *subject = NULL;
    if (!send_ready_slot (&handler, &subject))
        return NULL;
    return handler;
}

void *zlink::socket_base_t::socket_msg_handler_subject () const
{
    return _socket_msg_handler_subject.load (std::memory_order_acquire);
}

void *zlink::socket_base_t::socket_send_ready_handler_subject () const
{
    zlink_send_ready_handler_fn handler = NULL;
    void *subject = NULL;
    if (!send_ready_slot (&handler, &subject))
        return NULL;
    return subject;
}

void zlink::socket_base_t::invoke_socket_msg_handler (
  zlink_socket_msg_handler_fn handler_,
  const zlink_routing_id_t *source_rid_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    if (!enter_callback_api ()) {
        for (size_t i = 0; i < part_count_; ++i) {
            const int rc = reinterpret_cast<msg_t *> (&parts_[i])->close ();
            errno_assert (rc == 0);
        }
        return;
    }
    socket_base_t *previous = g_current_socket_msg_dispatch_socket;
    void *previous_subject = g_current_socket_msg_dispatch_subject;
    zlink_routing_id_t previous_source_rid;
    const bool previous_source_rid_valid =
      g_current_socket_msg_dispatch_source_rid_valid;
    if (previous_source_rid_valid)
        previous_source_rid = g_current_socket_msg_dispatch_source_rid;
    g_current_socket_msg_dispatch_socket = this;
    g_current_socket_msg_dispatch_subject = socket_msg_handler_subject ();
    if (source_rid_) {
        g_current_socket_msg_dispatch_source_rid = *source_rid_;
        g_current_socket_msg_dispatch_source_rid_valid = true;
    } else {
        memset (&g_current_socket_msg_dispatch_source_rid, 0,
                sizeof (g_current_socket_msg_dispatch_source_rid));
        g_current_socket_msg_dispatch_source_rid_valid = false;
    }
    handler_ (source_rid_, parts_, part_count_);
    g_current_socket_msg_dispatch_socket = previous;
    g_current_socket_msg_dispatch_subject = previous_subject;
    if (previous_source_rid_valid) {
        g_current_socket_msg_dispatch_source_rid = previous_source_rid;
        g_current_socket_msg_dispatch_source_rid_valid = true;
    } else {
        memset (&g_current_socket_msg_dispatch_source_rid, 0,
                sizeof (g_current_socket_msg_dispatch_source_rid));
        g_current_socket_msg_dispatch_source_rid_valid = false;
    }
    leave_callback_api ();
}

void zlink::socket_base_t::close_socket_msg_parts (
  std::vector<zlink_msg_t> *parts_)
{
    if (!parts_)
        return;

    for (size_t i = 0; i < parts_->size (); ++i) {
        const int rc =
          reinterpret_cast<msg_t *> (&(*parts_)[i])->close ();
        errno_assert (rc == 0);
    }
    parts_->clear ();
}

void zlink::socket_base_t::resolve_socket_msg_source_rid (
  pipe_t *pipe_, zlink_routing_id_t *out_)
{
    if (!out_)
        return;

    memset (out_, 0, sizeof (*out_));

    pipe_t *routing_pipe = pipe_;
    if (routing_pipe && routing_pipe->get_peer ())
        routing_pipe = routing_pipe->get_peer ();

    if (!routing_pipe)
        return;

    copy_routing_id (out_, routing_pipe->get_routing_id ());
}

void zlink::socket_base_t::dispatch_spot_handler_from_io (
  const zlink_routing_id_t *source_rid_,
  const char *topic_,
  size_t topic_len_,
  zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_)
{
    socket_base_t *self = static_cast<socket_base_t *> (userdata_);
    if (!self) {
        for (size_t i = 0; i < part_count_; ++i) {
            const int rc = reinterpret_cast<msg_t *> (&parts_[i])->close ();
            errno_assert (rc == 0);
        }
        return;
    }

    zlink_spot_handler_fn handler = self->socket_spot_handler ();
    if (!handler) {
        for (size_t i = 0; i < part_count_; ++i) {
            const int rc = reinterpret_cast<msg_t *> (&parts_[i])->close ();
            errno_assert (rc == 0);
        }
        return;
    }

    if (!self->enter_callback_api ()) {
        for (size_t i = 0; i < part_count_; ++i) {
            const int rc = reinterpret_cast<msg_t *> (&parts_[i])->close ();
            errno_assert (rc == 0);
        }
        return;
    }

    handler (source_rid_, topic_, topic_len_, parts_, part_count_);
    self->leave_callback_api ();
}

void zlink::socket_base_t::arm_send_ready_notification ()
{
    if (socket_send_ready_handler ())
        _send_ready_armed.store (true, std::memory_order_release);
}

void zlink::socket_base_t::notify_send_ready_if_armed ()
{
    if (!_send_ready_armed.load (std::memory_order_acquire) || !has_out ())
        return;

    bool expected = true;
    if (!_send_ready_armed.compare_exchange_strong (
          expected, false, std::memory_order_acq_rel,
          std::memory_order_acquire))
        return;

    zlink_send_ready_handler_fn handler = socket_send_ready_handler ();
    if (handler) {
        invoke_send_ready_handler_for_testing ();
    }
}

int zlink::socket_base_t::sub_dispatch_start (spot_sub_io_handler_fn callback_,
                                              void *userdata_)
{
    LIBZLINK_UNUSED (callback_);
    LIBZLINK_UNUSED (userdata_);
    errno = ENOTSUP;
    return -1;
}

int zlink::socket_base_t::sub_dispatch_stop ()
{
    errno = ENOTSUP;
    return -1;
}

bool zlink::socket_base_t::sub_dispatch_active () const
{
    return false;
}

int zlink::socket_base_t::xpub_dispatch_start ()
{
    errno = ENOTSUP;
    return -1;
}

bool zlink::socket_base_t::xpub_dispatch_active () const
{
    return false;
}

int zlink::socket_base_t::stream_dispatch_start_raw (
  zlink_stream_on_raw_fn callback_)
{
    LIBZLINK_UNUSED (callback_);
    errno = ENOTSUP;
    return -1;
}

int zlink::socket_base_t::stream_dispatch_stop ()
{
    errno = ENOTSUP;
    return -1;
}

bool zlink::socket_base_t::stream_dispatch_active () const
{
    return false;
}

bool zlink::socket_base_t::stream_dispatch_in_callback () const
{
    return false;
}

int zlink::socket_base_t::stream_dispatch_send_from_io (
  const zlink_routing_id_t *,
  const void *,
  size_t,
  int)
{
    return 0;
}

int zlink::socket_base_t::stream_dispatch_send_msg_from_io (
  const zlink_routing_id_t *,
  msg_t *,
  int)
{
    return 0;
}

std::recursive_mutex *zlink::socket_base_t::api_sync_mutex ()
{
    return NULL;
}

int zlink::socket_base_t::close ()
{
    const bool from_self_callback = send_ready_dispatch_in_callback ();
    if (!begin_close_or_fail_busy (from_self_callback))
        return -1;
    if (from_self_callback)
        return 0;

    finish_close_handoff ();
    return 0;
}

bool zlink::socket_base_t::has_in ()
{
    return xhas_in ();
}

bool zlink::socket_base_t::has_out ()
{
    return xhas_out ();
}

void zlink::socket_base_t::reaper_mailbox_handler (void *arg_)
{
    socket_base_t *self = static_cast<socket_base_t *> (arg_);
    self->in_event ();
    self->dec_mailbox_ref ();
}

void zlink::socket_base_t::reaper_mailbox_pre_post (void *arg_)
{
    socket_base_t *self = static_cast<socket_base_t *> (arg_);
    self->inc_mailbox_ref ();
}

void zlink::socket_base_t::async_mailbox_handler (void *arg_)
{
    socket_base_t *self = static_cast<socket_base_t *> (arg_);
    self->process_async_mailbox ();
    self->dec_mailbox_ref ();
}

void zlink::socket_base_t::async_mailbox_pre_post (void *arg_)
{
    socket_base_t *self = static_cast<socket_base_t *> (arg_);
    self->inc_mailbox_ref ();
}

void zlink::socket_base_t::start_reaping (poller_t *poller_)
{
    //  Safety net: if the async mailbox handler is still running
    //  (e.g. close() was bypassed or quiesce timed out), wait here
    //  before the reaper touches socket internal state.
    if (_async_quiesce_pending.load (std::memory_order_acquire))
        wait_async_quiesced (1000);

    //  Plug the socket to the reaper thread.
    _poller = poller_;

    mailbox_t *mailbox = static_cast<mailbox_t *> (_mailbox);
    mailbox->set_io_context (&_poller->get_io_context (),
                             &socket_base_t::reaper_mailbox_handler, this,
                             &socket_base_t::reaper_mailbox_pre_post);
    mailbox->schedule_if_needed ();

    //  Initialise the termination and check whether it can be deallocated
    //  immediately.
    terminate ();
    check_destroy ();
}

int zlink::socket_base_t::process_commands (int timeout_, bool throttle_)
{
    if (timeout_ == 0) {
        //  If we are asked not to wait, check whether we haven't processed
        //  commands recently, so that we can throttle the new commands.

        //  Get the CPU's tick counter. If 0, the counter is not available.
        const uint64_t tsc = zlink::clock_t::rdtsc ();

        //  Optimised version of command processing - it doesn't have to check
        //  for incoming commands each time. It does so only if certain time
        //  elapsed since last command processing. Command delay varies
        //  depending on CPU speed: It's ~1ms on 3GHz CPU, ~2ms on 1.5GHz CPU
        //  etc. The optimisation makes sense only on platforms where getting
        //  a timestamp is a very cheap operation (tens of nanoseconds).
        if (tsc && throttle_) {
            //  Check whether TSC haven't jumped backwards (in case of migration
            //  between CPU cores) and whether certain time have elapsed since
            //  last command processing. If it didn't do nothing.
            if (tsc >= _last_tsc && tsc - _last_tsc <= max_command_delay)
                return 0;
            _last_tsc = tsc;
        }
    }

    //  Check whether there are any commands pending for this thread.
    command_t cmd;
    int rc = _mailbox->recv (&cmd, timeout_);

    if (rc != 0 && errno == EINTR)
        return -1;

    //  Process all available commands.
    while (rc == 0 || errno == EINTR) {
        if (rc == 0) {
            cmd.destination->process_command (cmd);
        }
        rc = _mailbox->recv (&cmd, 0);
    }

    zlink_assert (errno == EAGAIN);

    if (_ctx_terminated) {
        errno = ETERM;
        return -1;
    }

    return 0;
}

int zlink::socket_base_t::start_async_mailbox_processing (
  io_thread_t *io_thread_)
{
    if (!io_thread_) {
        errno = EINVAL;
        return -1;
    }

    mailbox_t *mailbox = static_cast<mailbox_t *> (_mailbox);
    _async_mailbox_active.store (true, std::memory_order_release);
    mailbox->set_io_context (&io_thread_->get_io_context (),
                             &socket_base_t::async_mailbox_handler, this,
                             &socket_base_t::async_mailbox_pre_post);
    mailbox->schedule_if_needed ();
    return 0;
}

void zlink::socket_base_t::stop_async_mailbox_processing ()
{
    _async_mailbox_active.store (false, std::memory_order_release);
    _async_processing_done.store (false, std::memory_order_release);
    _async_quiesce_pending.store (true, std::memory_order_release);
    mailbox_t *mailbox = static_cast<mailbox_t *> (_mailbox);
    mailbox->schedule_if_needed ();
}

void zlink::socket_base_t::wait_async_quiesced (int timeout_ms_)
{
    if (_async_processing_done.load (std::memory_order_acquire))
        return;
    scoped_lock_t lock (_async_done_mu);
    while (!_async_processing_done.load (std::memory_order_acquire)) {
        const int rc =
          _async_done_cv.wait (&_async_done_mu,
                               timeout_ms_ > 0 ? timeout_ms_ : 2000);
        if (rc != 0)
            break;
    }
}

void zlink::socket_base_t::process_stop ()
{
    //  Here, someone have called zlink_ctx_term while the socket was still alive.
    //  We'll remember the fact so that any blocking call is interrupted and any
    //  further attempt to use the socket will return ETERM. The user is still
    //  responsible for calling zlink_close on the socket though!
    scoped_lock_t lock (_monitor_sync);
    stop_monitor ();

    _ctx_terminated = true;
}

void zlink::socket_base_t::process_bind (pipe_t *pipe_)
{
    attach_pipe (pipe_);
}

void zlink::socket_base_t::process_term (int linger_)
{
    if (getenv ("ZLINK_DEBUG_SOCKET_TERM")) {
        fprintf (stderr,
                 "[socket-term] begin socket=%p id=%d type=%d pipes=%zu linger=%d\n",
                 static_cast<void *> (this), options.socket_id, options.type,
                 static_cast<size_t> (_pipes.size ()), linger_);
        for (pipes_t::size_type i = 0, size = _pipes.size (); i != size; ++i) {
            fprintf (stderr,
                     "[socket-term]   pipe socket=%d ptr=%p endpoint=%s\n",
                     options.socket_id, static_cast<void *> (_pipes[i]),
                     _pipes[i]
                       ? _pipes[i]->get_endpoint_pair ().identifier ().c_str ()
                       : "<null>");
        }
        fflush (stderr);
    }

    //  Unregister all inproc endpoints associated with this socket.
    //  Doing this we make sure that no new pipes from other sockets (inproc)
    //  will be initiated.
    unregister_endpoints (this);

    //  Ask all attached pipes to terminate.
    for (pipes_t::size_type i = 0, size = _pipes.size (); i != size; ++i) {
        //  Only inprocs might have a disconnect message set
        _pipes[i]->send_disconnect_msg ();
        _pipes[i]->terminate (false);
    }
    register_term_acks (static_cast<int> (_pipes.size ()));
#ifndef NDEBUG
    _term_pipe_acks_registered = static_cast<int> (_pipes.size ());
    _term_pipe_acks_received = 0;
#endif

    //  Continue the termination process immediately.
    own_t::process_term (linger_);
}

void zlink::socket_base_t::process_term_endpoint (std::string *endpoint_)
{
    term_endpoint (endpoint_->c_str ());
    delete endpoint_;
}

void zlink::socket_base_t::set_all_pipes_nodelay ()
{
    for (pipes_t::size_type i = 0, size = _pipes.size (); i != size; ++i) {
        if (_pipes[i])
            _pipes[i]->set_nodelay ();
    }
}


void zlink::socket_base_t::update_pipe_options (int option_)
{
    if (option_ == ZLINK_SNDHWM || option_ == ZLINK_RCVHWM) {
        for (pipes_t::size_type i = 0, size = _pipes.size (); i != size; ++i) {
            _pipes[i]->set_hwms (options.rcvhwm, options.sndhwm);
            _pipes[i]->send_hwms_to_peer (options.sndhwm, options.rcvhwm);
        }
    }
}

void zlink::socket_base_t::process_destroy ()
{
    if (getenv ("ZLINK_DEBUG_SOCKET_TERM")) {
        fprintf (stderr,
                 "[socket-term] destroy socket=%p id=%d type=%d destroyed=%d\n",
                 static_cast<void *> (this), options.socket_id, options.type,
                 _destroyed ? 1 : 0);
        fflush (stderr);
    }
#ifndef NDEBUG
    if (_term_pipe_acks_registered != _term_pipe_acks_received) {
        fprintf (stderr,
                 "[term-diag] socket %p id=%d type=%d pipe_acks: "
                 "registered=%d received=%d\n",
                 static_cast<void *> (this), options.socket_id, options.type,
                 _term_pipe_acks_registered, _term_pipe_acks_received);
    }
#endif
    _destroyed = true;
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

int zlink::socket_base_t::xsocket_msg_dispatch (msg_t *msg_, pipe_t *pipe_)
{
    LIBZLINK_UNUSED (msg_);
    LIBZLINK_UNUSED (pipe_);
    return 0;
}

int zlink::socket_base_t::xstream_dispatch_msg (msg_t *msg_, pipe_t *pipe_)
{
    LIBZLINK_UNUSED (msg_);
    LIBZLINK_UNUSED (pipe_);
    return 0;
}

void zlink::socket_base_t::xdispatch_io ()
{
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

void zlink::socket_base_t::in_event ()
{
    do {
        //  This function is invoked only once the socket is running in the
        //  context of the reaper thread. Process any commands from other
        //  threads/sockets that may be available at the moment. Ultimately,
        //  the socket will be destroyed.
        {
            process_commands (0, false);
        }
        if (_destroyed) {
            check_destroy ();
            return;
        }
    } while (static_cast<mailbox_t *> (_mailbox)->reschedule_if_needed ());
}

void zlink::socket_base_t::process_async_mailbox ()
{
    do {
        process_commands (0, false);
        if (_destroyed) {
            check_destroy ();
            return;
        }
        if (_async_mailbox_active.load (std::memory_order_acquire))
            xdispatch_io ();
        if (!_async_mailbox_active.load (std::memory_order_acquire)) {
            mailbox_t *mailbox = static_cast<mailbox_t *> (_mailbox);
            mailbox->reschedule_if_needed ();
            mailbox->set_io_context (NULL, NULL, NULL, NULL);
            //  Signal quiesce completion to waiting close()/start_reaping().
            if (_async_quiesce_pending.load (std::memory_order_acquire)) {
                _async_quiesce_pending.store (false,
                                              std::memory_order_release);
                _async_processing_done.store (true,
                                              std::memory_order_release);
                scoped_lock_t lock (_async_done_mu);
                _async_done_cv.broadcast ();
            }
            return;
        }
    } while (static_cast<mailbox_t *> (_mailbox)->reschedule_if_needed ());
}

void zlink::socket_base_t::out_event ()
{
    zlink_assert (false);
}

void zlink::socket_base_t::timer_event (int)
{
    zlink_assert (false);
}

void zlink::socket_base_t::check_destroy ()
{
    //  If the object was already marked as destroyed, finish the deallocation.
    if (_destroyed) {
        _destroy_pending = true;
        if (_mailbox_refcnt.add (0) != 0)
            return;

        inc_mailbox_ref ();
        if (_poller) {
            boost::asio::post (_poller->get_io_context (), [this]() {
                this->dec_mailbox_ref ();
            });
        } else {
            dec_mailbox_ref ();
        }
    }
}

void zlink::socket_base_t::inc_mailbox_ref ()
{
    _mailbox_refcnt.add (1);
}

void zlink::socket_base_t::dec_mailbox_ref ()
{
    if (_mailbox_refcnt.sub (1) || !_destroy_pending)
        return;

    finalize_destroy ();
}

void zlink::socket_base_t::finalize_destroy ()
{
    _destroy_pending = false;

    //  Remove the socket from the context.
    destroy_socket (this);

    //  Notify the reaper about the fact.
    send_reaped ();

    //  Deallocate.
    own_t::process_destroy ();
}

void zlink::socket_base_t::read_activated (pipe_t *pipe_)
{
    xread_activated (pipe_);
}

void zlink::socket_base_t::write_activated (pipe_t *pipe_)
{
    xwrite_activated (pipe_);
    notify_send_ready_if_armed ();
}

void zlink::socket_base_t::hiccuped (pipe_t *pipe_)
{
    if (options.immediate == 1)
        pipe_->terminate (false);
    else
        // Notify derived sockets of the hiccup
        xhiccuped (pipe_);
}

void zlink::socket_base_t::pipe_terminated (pipe_t *pipe_)
{
    if (getenv ("ZLINK_DEBUG_SOCKET_TERM")) {
        fprintf (stderr,
                 "[socket-term] pipe-terminated socket=%p id=%d type=%d pipe=%p terminating=%d\n",
                 static_cast<void *> (this), options.socket_id, options.type,
                 static_cast<void *> (pipe_), is_terminating () ? 1 : 0);
        fflush (stderr);
    }

    //  Notify the specific socket type about the pipe termination.
    xpipe_terminated (pipe_);

    // Remove pipe from inproc pipes
    _inprocs.erase_pipe (pipe_);

    //  Remove the pipe from the list of attached pipes and confirm its
    //  termination if we are already shutting down.
    _pipes.erase (pipe_);

    // Remove the pipe from _endpoints (set it to NULL).
    const std::string &identifier = pipe_->get_endpoint_pair ().identifier ();
    if (!identifier.empty ()) {
        std::pair<endpoints_t::iterator, endpoints_t::iterator> range;
        range = _endpoints.equal_range (identifier);

        for (endpoints_t::iterator it = range.first; it != range.second; ++it) {
            if (it->second.second == pipe_) {
                it->second.second = NULL;
                break;
            }
        }
    }

    if (is_terminating ()) {
#ifndef NDEBUG
        ++_term_pipe_acks_received;
#endif
        unregister_term_ack ();
    }
}

void zlink::socket_base_t::extract_flags (const msg_t *msg_)
{
    //  Test whether routing_id flag is valid for this socket type.
    if (unlikely (msg_->flags () & msg_t::routing_id))
        zlink_assert (options.recv_routing_id);

    //  Remove MORE flag.
    _rcvmore = (msg_->flags () & msg_t::more) != 0;
}

int zlink::socket_base_t::monitor (const char *endpoint_,
                                 uint64_t events_,
                                 int event_version_,
                                 int type_)
{
    scoped_lock_t lock (_monitor_sync);

    if (unlikely (_ctx_terminated)) {
        errno = ETERM;
        return -1;
    }

    //  Support deregistering monitoring endpoints as well
    if (endpoint_ == NULL) {
        stop_monitor ();
        return 0;
    }
    //  Parse endpoint_uri_ string.
    std::string protocol;
    std::string address;
    if (parse_uri (endpoint_, protocol, address) || check_protocol (protocol))
        return -1;

    //  Event notification only supported over inproc://
    if (protocol != protocol_name::inproc) {
        errno = EPROTONOSUPPORT;
        return -1;
    }

    // already monitoring. Stop previous monitor before starting new one.
    if (_monitor_socket != NULL) {
        stop_monitor (true);
    }

    // Check if the specified socket type is supported. It must be a
    // one-way socket types that support the SNDMORE flag.
    switch (type_) {
        case ZLINK_PAIR:
            break;
        case ZLINK_PUB:
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    //  Register events to monitor
    _monitor_events = events_;
    _monitor_lossy = event_version_ <= 3;
    //  Create a monitor socket of the specified type.
    _monitor_socket = static_cast<void *> (get_ctx ()->create_socket (type_));
    if (_monitor_socket == NULL)
        return -1;

    //  Never block context termination on pending event messages
    int linger = 0;
    int rc =
      zlink_setsockopt (_monitor_socket, ZLINK_LINGER, &linger, sizeof (linger));
    if (rc == -1)
        stop_monitor (false);

    //  Spawn the monitor socket endpoint
    rc = zlink_bind (_monitor_socket, endpoint_);
    if (rc == -1)
        stop_monitor (false);
    else {
        _monitor_queue_sync.lock ();
        _monitor_queue.clear ();
        _monitor_queue_stop = false;
        _monitor_queue_sync.unlock ();
        _monitor_thread.start (&socket_base_t::monitor_thread_main, this,
                               "sock-monitor");
        _monitor_thread_started = true;
        _monitor_events_atomic.store (_monitor_events,
                                      std::memory_order_release);
    }
    return rc;
}

void zlink::socket_base_t::event_connected (
  const endpoint_uri_pair_t &endpoint_uri_pair_, zlink::fd_t fd_)
{
    uint64_t values[1] = {static_cast<uint64_t> (fd_)};
    event (endpoint_uri_pair_, NULL, 0, values, 1, ZLINK_EVENT_CONNECTED);
}

void zlink::socket_base_t::event_connect_delayed (
  const endpoint_uri_pair_t &endpoint_uri_pair_, int err_)
{
    uint64_t values[1] = {static_cast<uint64_t> (err_)};
    event (endpoint_uri_pair_, NULL, 0, values, 1, ZLINK_EVENT_CONNECT_DELAYED);
}

void zlink::socket_base_t::event_connect_retried (
  const endpoint_uri_pair_t &endpoint_uri_pair_, int interval_)
{
    uint64_t values[1] = {static_cast<uint64_t> (interval_)};
    event (endpoint_uri_pair_, NULL, 0, values, 1, ZLINK_EVENT_CONNECT_RETRIED);
}

void zlink::socket_base_t::event_listening (
  const endpoint_uri_pair_t &endpoint_uri_pair_, zlink::fd_t fd_)
{
    uint64_t values[1] = {static_cast<uint64_t> (fd_)};
    event (endpoint_uri_pair_, NULL, 0, values, 1, ZLINK_EVENT_LISTENING);
}

void zlink::socket_base_t::event_bind_failed (
  const endpoint_uri_pair_t &endpoint_uri_pair_, int err_)
{
    uint64_t values[1] = {static_cast<uint64_t> (err_)};
    event (endpoint_uri_pair_, NULL, 0, values, 1, ZLINK_EVENT_BIND_FAILED);
}

void zlink::socket_base_t::event_accepted (
  const endpoint_uri_pair_t &endpoint_uri_pair_, zlink::fd_t fd_)
{
    uint64_t values[1] = {static_cast<uint64_t> (fd_)};
    event (endpoint_uri_pair_, NULL, 0, values, 1, ZLINK_EVENT_ACCEPTED);
}

void zlink::socket_base_t::event_accept_failed (
  const endpoint_uri_pair_t &endpoint_uri_pair_, int err_)
{
    uint64_t values[1] = {static_cast<uint64_t> (err_)};
    event (endpoint_uri_pair_, NULL, 0, values, 1, ZLINK_EVENT_ACCEPT_FAILED);
}

void zlink::socket_base_t::event_closed (
  const endpoint_uri_pair_t &endpoint_uri_pair_, zlink::fd_t fd_)
{
    uint64_t values[1] = {static_cast<uint64_t> (fd_)};
    event (endpoint_uri_pair_, NULL, 0, values, 1, ZLINK_EVENT_CLOSED);
}

void zlink::socket_base_t::event_close_failed (
  const endpoint_uri_pair_t &endpoint_uri_pair_, int err_)
{
    uint64_t values[1] = {static_cast<uint64_t> (err_)};
    event (endpoint_uri_pair_, NULL, 0, values, 1, ZLINK_EVENT_CLOSE_FAILED);
}

void zlink::socket_base_t::event_disconnected (
  const endpoint_uri_pair_t &endpoint_uri_pair_,
  uint64_t reason_,
  const unsigned char *routing_id_,
  size_t routing_id_size_)
{
    uint64_t values[1] = {reason_};
    event (endpoint_uri_pair_, routing_id_, routing_id_size_, values, 1,
           ZLINK_EVENT_DISCONNECTED);
}

void zlink::socket_base_t::event_handshake_failed_no_detail (
  const endpoint_uri_pair_t &endpoint_uri_pair_, int err_)
{
    uint64_t values[1] = {static_cast<uint64_t> (err_)};
    event (endpoint_uri_pair_, NULL, 0, values, 1,
           ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL);
}

void zlink::socket_base_t::event_handshake_failed_protocol (
  const endpoint_uri_pair_t &endpoint_uri_pair_, int err_)
{
    uint64_t values[1] = {static_cast<uint64_t> (err_)};
    event (endpoint_uri_pair_, NULL, 0, values, 1,
           ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL);
}

void zlink::socket_base_t::event_handshake_failed_auth (
  const endpoint_uri_pair_t &endpoint_uri_pair_, int err_)
{
    uint64_t values[1] = {static_cast<uint64_t> (err_)};
    event (endpoint_uri_pair_, NULL, 0, values, 1,
           ZLINK_EVENT_HANDSHAKE_FAILED_AUTH);
}

void zlink::socket_base_t::event_connection_ready (
  const endpoint_uri_pair_t &endpoint_uri_pair_,
  const unsigned char *routing_id_,
  size_t routing_id_size_)
{
    uint64_t values[1] = {0};
    event (endpoint_uri_pair_, routing_id_, routing_id_size_, values, 1,
           ZLINK_EVENT_CONNECTION_READY);
}

void zlink::socket_base_t::emit_inproc_connection_ready (pipe_t *pipe_)
{
    if (!pipe_)
        return;

    if (!pipe_->mark_connection_ready_event_emitted ())
        return;

    const endpoint_uri_pair_t &endpoint_pair = pipe_->get_endpoint_pair ();
    const blob_t &routing_id = pipe_->get_routing_id ();
    const unsigned char *routing_id_data =
      routing_id.size () > 0 ? routing_id.data () : NULL;
    event_connection_ready (endpoint_pair, routing_id_data,
                            routing_id.size ());
}

void zlink::socket_base_t::event (const endpoint_uri_pair_t &endpoint_uri_pair_,
                                const unsigned char *routing_id_,
                                size_t routing_id_size_,
                                uint64_t values_[],
                                uint64_t values_count_,
                                uint64_t type_)
{
    if (_monitor_events_atomic.load (std::memory_order_acquire) == 0)
        return;

    scoped_lock_t lock (_monitor_sync);
    if (_monitor_events & type_) {
        monitor_event_record_t record;
        if (build_monitor_event_record (&record, type_, values_, values_count_,
                                        routing_id_, routing_id_size_,
                                        endpoint_uri_pair_))
            enqueue_monitor_event (record);
    }
}

void zlink::socket_base_t::monitor_thread_main (void *arg_)
{
    static_cast<socket_base_t *> (arg_)->monitor_loop ();
}

void zlink::socket_base_t::monitor_loop ()
{
    void *monitor_socket = _monitor_socket;
    _monitor_queue_sync.lock ();
    while (true) {
        if (_monitor_queue_stop)
            break;
        if (_monitor_queue.empty ()) {
            (void) _monitor_queue_cv.wait (&_monitor_queue_sync, -1);
            continue;
        }

        monitor_event_record_t record = _monitor_queue.front ();
        _monitor_queue.pop_front ();
        _monitor_queue_sync.unlock ();
        bool delivered = true;
        if (monitor_socket)
            delivered = dispatch_monitor_event (monitor_socket, record);
        if (!delivered && !_monitor_lossy) {
            _monitor_queue_sync.lock ();
            _monitor_queue.push_front (record);
            _monitor_queue_sync.unlock ();
            usleep (1000);
        }
        _monitor_queue_sync.lock ();
    }
    _monitor_queue_sync.unlock ();
}

void zlink::socket_base_t::enqueue_monitor_event (
  const monitor_event_record_t &record_)
{
    _monitor_queue_sync.lock ();
    if (!_monitor_queue_stop
        && (_monitor_queue.size () < static_cast<size_t> (monitor_queue_hwm)
            || !_monitor_lossy)) {
        _monitor_queue.push_back (record_);
        _monitor_queue_cv.broadcast ();
    }
    _monitor_queue_sync.unlock ();
}

bool zlink::socket_base_t::build_monitor_event_record (
  monitor_event_record_t *out_,
  uint64_t event_,
  const uint64_t values_[],
  uint64_t values_count_,
  const unsigned char *routing_id_,
  size_t routing_id_size_,
  const endpoint_uri_pair_t &endpoint_uri_pair_) const
{
    if (!out_ || values_count_ > monitor_max_values
        || routing_id_size_ > sizeof (out_->routing_id.data))
        return false;

    out_->event = event_;
    out_->values_count = values_count_;
    memset (out_->values, 0, sizeof (out_->values));
    for (uint64_t i = 0; i < values_count_; ++i)
        out_->values[i] = values_[i];
    memset (&out_->routing_id, 0, sizeof (out_->routing_id));
    out_->routing_id.size = static_cast<uint8_t> (routing_id_size_);
    if (routing_id_size_ > 0 && routing_id_)
        memcpy (out_->routing_id.data, routing_id_, routing_id_size_);
    out_->endpoint_uri_pair = endpoint_uri_pair_;
    return true;
}

bool zlink::socket_base_t::dispatch_monitor_event (
  void *monitor_socket_,
  const monitor_event_record_t &record_) const
{
    if (!monitor_socket_)
        return false;

    zlink_monitor_event_t wire_event;
    memset (&wire_event, 0, sizeof (wire_event));
    wire_event.event = record_.event;
    if (record_.values_count > 0)
        wire_event.value = record_.values[0];
    wire_event.routing_id = record_.routing_id;

    const size_t local_copy =
      record_.endpoint_uri_pair.local.size () < sizeof (wire_event.local_addr) - 1
        ? record_.endpoint_uri_pair.local.size ()
        : sizeof (wire_event.local_addr) - 1;
    if (local_copy > 0) {
        memcpy (wire_event.local_addr, record_.endpoint_uri_pair.local.data (),
                local_copy);
        wire_event.local_addr[local_copy] = '\0';
    }

    const size_t remote_copy =
      record_.endpoint_uri_pair.remote.size () < sizeof (wire_event.remote_addr) - 1
        ? record_.endpoint_uri_pair.remote.size ()
        : sizeof (wire_event.remote_addr) - 1;
    if (remote_copy > 0) {
        memcpy (wire_event.remote_addr, record_.endpoint_uri_pair.remote.data (),
                remote_copy);
        wire_event.remote_addr[remote_copy] = '\0';
    }

    zlink_msg_t msg;
    zlink_msg_init_size (&msg, sizeof (wire_event));
    memcpy (zlink_msg_data (&msg), &wire_event, sizeof (wire_event));
    const int send_flags = _monitor_lossy ? ZLINK_DONTWAIT : 0;
    if (zlink_msg_send (&msg, monitor_socket_, send_flags) == -1) {
        zlink_msg_close (&msg);
        return false;
    }
    return true;
}

void zlink::socket_base_t::stop_monitor (bool send_monitor_stopped_event_)
{
    // this is a private method which is only called from
    // contexts where the _monitor_sync mutex has been locked before

    if (_monitor_socket) {
        _monitor_events_atomic.store (0, std::memory_order_release);
        socket_base_t *monitor_socket =
          static_cast<socket_base_t *> (_monitor_socket);
        bool can_emit_monitor_stopped = false;

        if ((_monitor_events & ZLINK_EVENT_MONITOR_STOPPED)
            && send_monitor_stopped_event_) {
            monitor_socket->process_commands (0, false);
            can_emit_monitor_stopped = !monitor_socket->_pipes.empty ();
        }

        stop_monitor_thread ();

        if (can_emit_monitor_stopped) {
            uint64_t values[1] = {0};
            monitor_event_record_t record;
            if (build_monitor_event_record (&record, ZLINK_EVENT_MONITOR_STOPPED,
                                            values, 1, NULL, 0,
                                            endpoint_uri_pair_t ()))
                dispatch_monitor_event (_monitor_socket, record);
        }
        zlink_close (_monitor_socket);
        _monitor_socket = NULL;
        _monitor_events = 0;
        _monitor_lossy = true;
    }
}

void zlink::socket_base_t::stop_monitor_thread ()
{
    _monitor_queue_sync.lock ();
    _monitor_queue_stop = true;
    _monitor_queue.clear ();
    _monitor_queue_cv.broadcast ();
    _monitor_queue_sync.unlock ();

    if (_monitor_thread_started) {
        _monitor_thread.stop ();
        _monitor_thread_started = false;
    }
}

int zlink::socket_base_t::socket_id () const
{
    return options.socket_id;
}

bool zlink::socket_base_t::is_disconnected () const
{
    return _disconnected;
}

bool zlink::socket_base_t::is_ctx_terminated () const
{
    return _ctx_terminated;
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
        case ZLINK_CONNECT_ROUTING_ID:
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
