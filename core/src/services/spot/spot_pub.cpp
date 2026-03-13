/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_pub.hpp"
#include "services/common/monitor_decode.hpp"
#include "services/common/socket_monitor_bridge.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_runtime.hpp"

#include "sockets/socket_base.hpp"
#include "utils/err.hpp"
#include "utils/random.hpp"

#include <string.h>
#include <map>

namespace zlink
{
static const uint32_t spot_pub_tag_value = 0x1e6700db;

namespace
{
static void preserve_first_error (int rc_, int *first_error_)
{
    if (rc_ == 0 || !first_error_ || *first_error_ != 0)
        return;
    *first_error_ = errno != 0 ? errno : EIO;
}
}

namespace
{
struct spot_pub_send_ready_registry_t
{
    mutex_t sync;
    std::map<socket_base_t *, spot_pub_t *> pubs;
};

static spot_pub_send_ready_registry_t &spot_pub_send_ready_registry ()
{
    static spot_pub_send_ready_registry_t registry;
    return registry;
}

static void register_spot_pub_socket (socket_base_t *socket_, spot_pub_t *pub_)
{
    if (!socket_ || !pub_)
        return;

    spot_pub_send_ready_registry_t &registry = spot_pub_send_ready_registry ();
    scoped_lock_t lock (registry.sync);
    registry.pubs[socket_] = pub_;
}

static void unregister_spot_pub_socket (socket_base_t *socket_)
{
    if (!socket_)
        return;

    spot_pub_send_ready_registry_t &registry = spot_pub_send_ready_registry ();
    scoped_lock_t lock (registry.sync);
    registry.pubs.erase (socket_);
}

static spot_pub_t *find_spot_pub_for_socket (socket_base_t *socket_)
{
    if (!socket_)
        return NULL;

    spot_pub_send_ready_registry_t &registry = spot_pub_send_ready_registry ();
    scoped_lock_t lock (registry.sync);
    std::map<socket_base_t *, spot_pub_t *>::iterator it =
      registry.pubs.find (socket_);
    return it != registry.pubs.end () ? it->second : NULL;
}

static void spot_pub_send_ready_adapter (void *subject_)
{
    spot_pub_t *pub = find_spot_pub_for_socket (
      static_cast<socket_base_t *> (subject_));
    if (pub)
        pub->dispatch_send_ready ();
}
}

static void fill_terminal_monitor_event (zlink_service_event_t *event_,
                                         uint32_t event_type_,
                                         const zlink_routing_id_t &rid_)
{
    memset (event_, 0, sizeof (*event_));
    event_->service_kind = ZLINK_SERVICE_KIND_SPOT_PUB;
    event_->event_type = event_type_;
    event_->detail_flags = ZLINK_EVENT_DETAIL_SUBJECT_RID;
    event_->routing_id = rid_;
}

static void copy_endpoint (char *dst_, size_t dst_size_, const char *src_)
{
    if (!dst_ || dst_size_ == 0)
        return;
    dst_[0] = '\0';
    if (!src_ || src_[0] == '\0')
        return;
    const size_t copy_size = strlen (src_) < dst_size_ - 1 ? strlen (src_)
                                                           : dst_size_ - 1;
    if (copy_size > 0)
        memcpy (dst_, src_, copy_size);
    dst_[copy_size] = '\0';
}

static void copy_subject (char *dst_, size_t dst_size_, const char *src_)
{
    copy_endpoint (dst_, dst_size_, src_);
}

static void fill_socket_monitor_event (zlink_service_event_t *event_,
                                       uint32_t event_type_,
                                       const zlink_monitor_event_t &raw_)
{
    memset (event_, 0, sizeof (*event_));
    event_->service_kind = ZLINK_SERVICE_KIND_SPOT_PUB;
    event_->event_type = event_type_;
    event_->status = static_cast<int32_t> (raw_.event);
    event_->value = static_cast<uint32_t> (raw_.value);
    if (raw_.routing_id.size > 0) {
        event_->routing_id = raw_.routing_id;
        event_->detail_flags |= ZLINK_EVENT_DETAIL_PEER_RID;
    }
    if (raw_.remote_addr[0] != '\0') {
        copy_endpoint (event_->endpoint, sizeof (event_->endpoint),
                       raw_.remote_addr);
        event_->detail_flags |= ZLINK_EVENT_DETAIL_ENDPOINT;
    }
}

static void fill_subject_monitor_event (zlink_service_event_t *event_,
                                        uint32_t event_type_,
                                        const zlink_routing_id_t &rid_,
                                        const char *subject_,
                                        bool include_subject_kind_,
                                        uint32_t subject_kind_,
                                        uint32_t ready_count_)
{
    memset (event_, 0, sizeof (*event_));
    event_->service_kind = ZLINK_SERVICE_KIND_SPOT_PUB;
    event_->event_type = event_type_;
    event_->routing_id = rid_;
    event_->value = ready_count_;
    event_->detail_flags = ZLINK_EVENT_DETAIL_SUBJECT_RID;
    if (subject_ && subject_[0] != '\0') {
        copy_subject (event_->subject, sizeof (event_->subject), subject_);
        event_->detail_flags |= ZLINK_EVENT_DETAIL_SUBJECT;
    }
    if (include_subject_kind_) {
        event_->subject_kind = subject_kind_;
        event_->detail_flags |= ZLINK_EVENT_DETAIL_SUBJECT_KIND;
    }
}

spot_pub_t::spot_pub_t (spot_node_t *node_,
                        uint64_t attachment_id_,
                        bool node_owned_default_) :
    _node (node_),
    _attachment_id (attachment_id_),
    _tag (spot_pub_tag_value),
    _node_owned_default (node_owned_default_),
    _routing_id_locked (false),
    _send_ready_handler (NULL),
    _send_ready_subject (NULL),
    _monitor (node_ ? node_->ctx () : NULL),
    _raw_monitor_socket (NULL),
    _monitor_stop (0),
    _monitor_thread_started (false)
{
    memset (&_routing_id, 0, sizeof (_routing_id));
    initialize_routing_id (&_routing_id);
}

spot_pub_t::~spot_pub_t ()
{
    _tag = 0xdeadbeef;
}

bool spot_pub_t::check_tag () const
{
    return _tag == spot_pub_tag_value;
}

bool spot_pub_t::is_node_owned_default () const
{
    return _node_owned_default;
}

socket_base_t *spot_pub_t::socket () const
{
    if (!_node || !_node->_runtime)
        return NULL;
    return _node->_runtime->attachment_socket (_attachment_id);
}

int spot_pub_t::initialize_routing_id (zlink_routing_id_t *out_)
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }

    const uint32_t value = generate_random ();
    out_->size = sizeof (value);
    memcpy (out_->data, &value, sizeof (value));
    return 0;
}

void spot_pub_t::lock_routing_id ()
{
    _routing_id_locked = true;
}

void spot_pub_t::submit_error_summary (int error_code_)
{
    if (_node)
        _node->submit_pub_summary (this, ZLINK_TOPOLOGY_STATE_ERROR,
                                   error_code_);
}

int spot_pub_t::publish (const char *topic_,
                         zlink_msg_t *parts_,
                         size_t part_count_,
                         int flags_)
{
    socket_base_t *socket = this->socket ();
    if (!_node || !socket) {
        errno = EFAULT;
        return -1;
    }
    if (!topic_ || topic_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (part_count_ > 0 && !parts_) {
        errno = EINVAL;
        return -1;
    }
    if (_node->ensure_healthy () != 0)
        return -1;

    int saved_errno = 0;
    {
        scoped_lock_t lock (_sync);
        lock_routing_id ();

        msg_t topic_msg;
        if (topic_msg.init_size (strlen (topic_)) != 0)
            return -1;
        memcpy (topic_msg.data (), topic_, strlen (topic_));
        if (socket->send (
              &topic_msg,
              part_count_ > 0 ? ZLINK_SNDMORE : (flags_ & ZLINK_DONTWAIT))
            != 0) {
            saved_errno = errno;
            topic_msg.close ();
        } else {
            topic_msg.close ();
        }

        for (size_t i = 0; saved_errno == 0 && i < part_count_; ++i) {
            const int send_flags = (i + 1 < part_count_ ? ZLINK_SNDMORE : 0)
                                   | (flags_ & ZLINK_DONTWAIT);
            msg_t *part = reinterpret_cast<msg_t *> (&parts_[i]);
            if (socket->send (part, send_flags) != 0)
                saved_errno = errno;
        }
    }

    if (saved_errno != 0) {
        if (saved_errno != EAGAIN)
            submit_error_summary (saved_errno);
        errno = saved_errno;
        return -1;
    }

    return 0;
}

int spot_pub_t::set_option (int option_,
                            const void *optval_,
                            size_t optvallen_)
{
    socket_base_t *socket = this->socket ();
    if (!socket) {
        errno = EFAULT;
        return -1;
    }
    if (!optval_ || optvallen_ == 0) {
        errno = EINVAL;
        return -1;
    }

    int socket_option = -1;
    switch (option_) {
        case ZLINK_SPOT_PUB_OPT_SNDHWM:
            socket_option = ZLINK_SNDHWM;
            break;
        case ZLINK_SPOT_PUB_OPT_SNDTIMEO:
            socket_option = ZLINK_SNDTIMEO;
            break;
        case ZLINK_SPOT_PUB_OPT_LINGER:
            socket_option = ZLINK_LINGER;
            break;
        case ZLINK_SPOT_PUB_OPT_SNDBUF:
            socket_option = ZLINK_SNDBUF;
            break;
        case ZLINK_SPOT_PUB_OPT_RCVBUF:
            socket_option = ZLINK_RCVBUF;
            break;
        case ZLINK_SPOT_PUB_OPT_NODROP:
            socket_option = ZLINK_XPUB_NODROP;
            break;
        case ZLINK_SPOT_PUB_OPT_MODE:
        case ZLINK_SPOT_PUB_OPT_QUEUE_HWM:
        case ZLINK_SPOT_PUB_OPT_QUEUE_FULL_POLICY:
            errno = ENOTSUP;
            return -1;
        default:
            errno = EINVAL;
            return -1;
    }

    scoped_lock_t lock (_sync);
    return socket->setsockopt (socket_option, optval_, optvallen_);
}

int spot_pub_t::set_send_ready_handler (zlink_send_ready_handler_fn handler_,
                                        void *subject_)
{
    socket_base_t *socket = this->socket ();
    if (!socket || !handler_ || !subject_) {
        errno = EINVAL;
        return -1;
    }

    register_spot_pub_socket (socket, this);
    if (socket->socket_set_send_ready_handler (&spot_pub_send_ready_adapter)
        != 0)
        return -1;

    _send_ready_subject.store (subject_, std::memory_order_release);
    _send_ready_handler.store (handler_, std::memory_order_release);
    return 0;
}

int spot_pub_t::routing_id (zlink_routing_id_t *out_) const
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    *out_ = _routing_id;
    return 0;
}

int spot_pub_t::peers (zlink_peer_info_t *peers_, size_t *count_) const
{
    socket_base_t *socket = this->socket ();
    if (!socket) {
        errno = EFAULT;
        return -1;
    }
    return zlink_socket_peers (static_cast<void *> (socket), peers_, count_);
}

void *spot_pub_t::monitor_open (int events_)
{
    lock_routing_id ();
    if (ensure_monitor_bridge_started () != 0)
        return NULL;
    return _monitor.open (events_);
}

void spot_pub_t::invoke_send_ready_for_testing ()
{
    socket_base_t *pub_socket = socket ();
    if (pub_socket)
        pub_socket->invoke_send_ready_handler_for_testing ();
}

void spot_pub_t::emit_delivery_ready_changed_event (const char *subject_,
                                                    bool include_subject_kind_,
                                                    uint32_t subject_kind_,
                                                    uint32_t ready_count_)
{
    zlink_service_event_t event;
    {
        scoped_lock_t lock (_sync);
        fill_subject_monitor_event (&event,
                                    ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED,
                                    _routing_id, subject_,
                                    include_subject_kind_, subject_kind_,
                                    ready_count_);
    }
    _monitor.emit (event);
}

void spot_pub_t::emit_ready_event ()
{
}

void spot_pub_t::dispatch_send_ready ()
{
    zlink_send_ready_handler_fn handler =
      _send_ready_handler.load (std::memory_order_acquire);
    void *subject = _send_ready_subject.load (std::memory_order_acquire);
    if (handler && subject)
        handler (subject);
}

void spot_pub_t::monitor_thread_main (void *arg_)
{
    static_cast<spot_pub_t *> (arg_)->monitor_loop ();
}

int spot_pub_t::ensure_monitor_bridge_started ()
{
    socket_base_t *socket = this->socket ();
    scoped_lock_t lock (_sync);
    if (_raw_monitor_socket)
        return 0;
    if (!socket) {
        errno = EFAULT;
        return -1;
    }

    void *monitor_socket = open_socket_monitor_bridge (
      socket, ZLINK_EVENT_CONNECTED | ZLINK_EVENT_ACCEPTED
                 | ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED
                 | ZLINK_EVENT_BIND_FAILED | ZLINK_EVENT_ACCEPT_FAILED
                 | ZLINK_EVENT_CLOSE_FAILED
                 | ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL
                 | ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL
                 | ZLINK_EVENT_HANDSHAKE_FAILED_AUTH);
    if (!monitor_socket)
        return -1;

    _raw_monitor_socket = monitor_socket;
    _monitor_stop.set (0);
    _monitor_thread.start (monitor_thread_main, this, "spot-pub-mon");
    _monitor_thread_started = true;
    return 0;
}

int spot_pub_t::stop_monitor_bridge ()
{
    void *raw_monitor_socket = NULL;
    ctx_t *ctx = _node ? _node->ctx () : NULL;
    socket_base_t *socket = this->socket ();
    int first_error = 0;
    {
        scoped_lock_t lock (_sync);
        _monitor_stop.set (1);
        raw_monitor_socket = _raw_monitor_socket;
        _raw_monitor_socket = NULL;
    }

    if (socket)
        preserve_first_error (socket->monitor (NULL, 0, 3, ZLINK_PAIR),
                              &first_error);

    if (_monitor_thread_started) {
        _monitor_thread.stop ();
        _monitor_thread_started = false;
    }
    if (raw_monitor_socket) {
        socket_base_t *monitor_socket =
          static_cast<socket_base_t *> (raw_monitor_socket);
        if (_node && ctx)
            preserve_first_error (
              _node->_lifecycle.close_socket_and_wait (monitor_socket, 2000),
                                  &first_error);
        else {
            monitor_socket->stop ();
            monitor_socket->close ();
            monitor_socket = NULL;
        }
    }
    if (first_error != 0) {
        errno = first_error;
        return -1;
    }
    return 0;
}

void spot_pub_t::monitor_loop ()
{
    while (_monitor_stop.get () == 0) {
        void *raw_monitor_socket = NULL;
        {
            scoped_lock_t lock (_sync);
            raw_monitor_socket = _raw_monitor_socket;
        }
        if (!raw_monitor_socket)
            return;

        if (zlink::wait_socket_events_internal (raw_monitor_socket,
                                                ZLINK_POLLIN, 50)
            <= 0)
            continue;

        zlink_monitor_event_t raw;
        if (recv_socket_monitor_event (raw_monitor_socket, &raw,
                                       ZLINK_DONTWAIT)
            != 0) {
            if (errno == EAGAIN)
                continue;
            if (_monitor_stop.get () != 0)
                return;
            continue;
        }

        zlink_service_event_t event;
        switch (raw.event) {
            case ZLINK_EVENT_CONNECTED:
            case ZLINK_EVENT_ACCEPTED:
                fill_socket_monitor_event (&event, ZLINK_MONITOR_EVENT_PEER_UP,
                                           raw);
                _monitor.emit (event);
                break;

            case ZLINK_EVENT_CONNECTION_READY:
                fill_socket_monitor_event (&event, ZLINK_MONITOR_EVENT_READY,
                                           raw);
                _monitor.emit (event);
                fill_socket_monitor_event (&event, ZLINK_MONITOR_EVENT_PEER_UP,
                                           raw);
                _monitor.emit (event);
                break;

            case ZLINK_EVENT_DISCONNECTED:
                fill_socket_monitor_event (&event, ZLINK_MONITOR_EVENT_LOST,
                                           raw);
                _monitor.emit (event);
                fill_socket_monitor_event (&event, ZLINK_MONITOR_EVENT_PEER_DOWN,
                                           raw);
                _monitor.emit (event);
                break;

            case ZLINK_EVENT_BIND_FAILED:
            case ZLINK_EVENT_ACCEPT_FAILED:
            case ZLINK_EVENT_CLOSE_FAILED:
            case ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL:
            case ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL:
            case ZLINK_EVENT_HANDSHAKE_FAILED_AUTH:
                fill_socket_monitor_event (&event, ZLINK_MONITOR_EVENT_ERROR,
                                           raw);
                event.error_code = static_cast<int32_t> (raw.value);
                _monitor.emit (event);
                submit_error_summary (static_cast<int> (raw.value));
                break;

            default:
                break;
        }
    }
}

int spot_pub_t::destroy_internal (bool allow_embedded_default_,
                                  bool notify_node_)
{
    if (_node_owned_default && !allow_embedded_default_) {
        errno = EINVAL;
        return -1;
    }

    socket_base_t *socket = this->socket ();
    int first_error = 0;

    if (notify_node_ && _node)
        _node->remove_spot_pub (this);
    if (notify_node_ && _node)
        _node->submit_pub_summary (this, ZLINK_TOPOLOGY_STATE_STOPPED, 0);

    preserve_first_error (stop_monitor_bridge (), &first_error);

    zlink_service_event_t terminal;
    fill_terminal_monitor_event (&terminal, ZLINK_MONITOR_EVENT_CLOSED,
                                 _routing_id);
    _monitor.close_all (&terminal);

    if (socket) {
        unregister_spot_pub_socket (socket);
        if (_node && _node->_runtime)
            preserve_first_error (
              _node->_runtime->destroy_attachment (_attachment_id), &first_error);
        else {
            socket->stop ();
            socket->close ();
        }
    }
    _attachment_id = 0;
    _node = NULL;
    _node_owned_default = false;
    if (first_error != 0) {
        errno = first_error;
        return -1;
    }
    return 0;
}

int spot_pub_t::destroy ()
{
    return destroy_internal (false, true);
}

int spot_pub_t::destroy_from_node ()
{
    return destroy_internal (true, true);
}

int spot_pub_t::abort_create ()
{
    return destroy_internal (true, false);
}
}
