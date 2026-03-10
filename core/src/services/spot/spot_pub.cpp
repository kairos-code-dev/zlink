/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_node.hpp"

#include "sockets/socket_base.hpp"
#include "utils/err.hpp"
#include "utils/random.hpp"

#include <string.h>

namespace zlink
{
static const uint32_t spot_pub_tag_value = 0x1e6700db;

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

spot_pub_t::spot_pub_t (spot_node_t *node_, socket_base_t *socket_) :
    _node (node_),
    _socket (socket_),
    _tag (spot_pub_tag_value),
    _routing_id_locked (false),
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
    if (!_node || !_socket) {
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
        if (_socket->send (
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
            if (_socket->send (part, send_flags) != 0)
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
    if (!_socket) {
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
    return _socket->setsockopt (socket_option, optval_, optvallen_);
}

int spot_pub_t::set_routing_id (const void *data_, size_t size_)
{
    if (!data_ || size_ == 0 || size_ > sizeof (_routing_id.data)) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_routing_id_locked) {
        errno = EFSM;
        return -1;
    }

    _routing_id.size = static_cast<uint8_t> (size_);
    memcpy (_routing_id.data, data_, size_);
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
    if (!_socket) {
        errno = EFAULT;
        return -1;
    }
    return zlink_socket_peers (static_cast<void *> (_socket), peers_, count_);
}

void *spot_pub_t::monitor_open (int events_)
{
    lock_routing_id ();
    if (ensure_monitor_bridge_started () != 0)
        return NULL;
    return _monitor.open (events_);
}

void *spot_pub_t::poller_socket ()
{
    lock_routing_id ();
    return static_cast<void *> (_socket);
}

void spot_pub_t::emit_ready_event ()
{
}

void spot_pub_t::monitor_thread_main (void *arg_)
{
    static_cast<spot_pub_t *> (arg_)->monitor_loop ();
}

int spot_pub_t::ensure_monitor_bridge_started ()
{
    scoped_lock_t lock (_sync);
    if (_raw_monitor_socket)
        return 0;
    if (!_socket) {
        errno = EFAULT;
        return -1;
    }

    void *monitor_socket = zlink_socket_monitor_open (
      static_cast<void *> (_socket),
      ZLINK_EVENT_CONNECTED | ZLINK_EVENT_ACCEPTED
        | ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED
        | ZLINK_EVENT_BIND_FAILED | ZLINK_EVENT_ACCEPT_FAILED
        | ZLINK_EVENT_CLOSE_FAILED | ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL
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

void spot_pub_t::stop_monitor_bridge ()
{
    void *raw_monitor_socket = NULL;
    {
        scoped_lock_t lock (_sync);
        _monitor_stop.set (1);
        raw_monitor_socket = _raw_monitor_socket;
        _raw_monitor_socket = NULL;
    }

    if (_monitor_thread_started) {
        _monitor_thread.stop ();
        _monitor_thread_started = false;
    }
    if (raw_monitor_socket)
        zlink_close (raw_monitor_socket);
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

        zlink_pollitem_t item;
        item.socket = raw_monitor_socket;
        item.fd = 0;
        item.events = ZLINK_POLLIN;
        item.revents = 0;
        const int poll_rc = zlink_poll (&item, 1, 50);
        if (poll_rc <= 0 || (item.revents & ZLINK_POLLIN) == 0)
            continue;

        zlink_monitor_event_t raw;
        if (zlink_monitor_recv (raw_monitor_socket, &raw, ZLINK_DONTWAIT) != 0) {
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

int spot_pub_t::destroy ()
{
    if (_node)
        _node->submit_pub_summary (this, ZLINK_TOPOLOGY_STATE_STOPPED, 0);
    if (_node)
        _node->remove_spot_pub (this);

    stop_monitor_bridge ();

    zlink_service_event_t terminal;
    fill_terminal_monitor_event (&terminal, ZLINK_MONITOR_EVENT_CLOSED,
                                 _routing_id);
    _monitor.close_all (&terminal);

    if (_socket) {
        _socket->close ();
        _socket = NULL;
    }
    _node = NULL;
    return 0;
}
}
