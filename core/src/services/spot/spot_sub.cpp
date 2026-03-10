/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_sub.hpp"
#include "services/spot/spot_node.hpp"

#include "sockets/socket_base.hpp"
#include "utils/err.hpp"
#include "utils/random.hpp"

#include <string.h>
#include <vector>

namespace zlink
{
static const uint32_t spot_sub_tag_value = 0x1e6700da;
static thread_local spot_sub_t *current_handler_dispatch_sub = NULL;

static void close_msgv (std::vector<zlink_msg_t> *parts_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < parts_->size (); ++i)
        zlink_msg_close (&(*parts_)[i]);
    parts_->clear ();
}

static void fill_terminal_monitor_event (zlink_service_event_t *event_,
                                         uint32_t event_type_,
                                         const zlink_routing_id_t &rid_)
{
    memset (event_, 0, sizeof (*event_));
    event_->service_kind = ZLINK_SERVICE_KIND_SPOT_SUB;
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
    event_->service_kind = ZLINK_SERVICE_KIND_SPOT_SUB;
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

spot_sub_t::spot_sub_t (spot_node_t *node_,
                        socket_base_t *socket_,
                        bool node_owned_default_) :
    _node (node_),
    _socket (socket_),
    _tag (spot_sub_tag_value),
    _node_owned_default (node_owned_default_),
    _routing_id_locked (false),
    _handler (NULL),
    _handler_userdata (NULL),
    _handler_state (handler_none),
    _callback_inflight (0),
    _recv_in_progress (0),
    _monitor (node_ ? node_->ctx () : NULL),
    _raw_monitor_socket (NULL),
    _monitor_stop (0),
    _monitor_thread_started (false)
{
    memset (&_routing_id, 0, sizeof (_routing_id));
    initialize_routing_id (&_routing_id);
}

spot_sub_t::~spot_sub_t ()
{
    _tag = 0xdeadbeef;
}

bool spot_sub_t::check_tag () const
{
    return _tag == spot_sub_tag_value;
}

bool spot_sub_t::is_node_owned_default () const
{
    return _node_owned_default;
}

int spot_sub_t::initialize_routing_id (zlink_routing_id_t *out_)
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

bool spot_sub_t::is_valid_topic (const char *topic_, std::string *out_)
{
    if (!topic_ || topic_[0] == '\0')
        return false;
    const size_t len = strlen (topic_);
    if (len == 0 || len > 255)
        return false;
    if (out_)
        *out_ = std::string (topic_, len);
    return true;
}

bool spot_sub_t::is_valid_pattern (const char *pattern_, std::string *prefix_out_)
{
    if (!pattern_ || pattern_[0] == '\0')
        return false;
    const size_t len = strlen (pattern_);
    if (len < 2 || len > 255 || pattern_[len - 1] != '*')
        return false;
    const char *star = strchr (pattern_, '*');
    if (star != pattern_ + len - 1)
        return false;
    if (prefix_out_)
        *prefix_out_ = std::string (pattern_, len - 1);
    return true;
}

void spot_sub_t::lock_routing_id ()
{
    _routing_id_locked = true;
}

int spot_sub_t::subscribe (const char *topic_)
{
    if (!_node || !_socket) {
        errno = EFAULT;
        return -1;
    }
    std::string topic;
    if (!is_valid_topic (topic_, &topic)) {
        errno = EINVAL;
        return -1;
    }
    if (_node->ensure_healthy () != 0)
        return -1;

    {
        scoped_lock_t lock (_sync);
        lock_routing_id ();
        if (_socket->setsockopt (ZLINK_SUBSCRIBE, topic.data (), topic.size ())
            != 0)
            return -1;
        _topics.insert (topic);
    }
    if (_node)
        _node->submit_sub_summary (this, ZLINK_TOPOLOGY_STATE_READY, 0);
    return 0;
}

int spot_sub_t::subscribe_pattern (const char *pattern_)
{
    if (!_node || !_socket) {
        errno = EFAULT;
        return -1;
    }
    std::string prefix;
    if (!is_valid_pattern (pattern_, &prefix)) {
        errno = EINVAL;
        return -1;
    }
    if (_node->ensure_healthy () != 0)
        return -1;

    {
        scoped_lock_t lock (_sync);
        lock_routing_id ();
        if (_socket->setsockopt (ZLINK_SUBSCRIBE, prefix.data (), prefix.size ())
            != 0)
            return -1;
        _patterns.insert (prefix);
    }
    if (_node)
        _node->submit_sub_summary (this, ZLINK_TOPOLOGY_STATE_READY, 0);
    return 0;
}

int spot_sub_t::unsubscribe (const char *topic_or_pattern_)
{
    if (!_node || !_socket) {
        errno = EFAULT;
        return -1;
    }
    std::string topic;
    std::string prefix;
    const bool is_pattern = is_valid_pattern (topic_or_pattern_, &prefix);
    const bool is_topic = !is_pattern && is_valid_topic (topic_or_pattern_, &topic);
    if (!is_pattern && !is_topic) {
        errno = EINVAL;
        return -1;
    }
    if (_node->ensure_healthy () != 0)
        return -1;

    bool has_filters_after = false;
    {
        scoped_lock_t lock (_sync);
        lock_routing_id ();
        const std::string &filter = is_pattern ? prefix : topic;
        if (_socket->setsockopt (ZLINK_UNSUBSCRIBE, filter.data (), filter.size ())
            != 0)
            return -1;
        if (is_pattern)
            _patterns.erase (prefix);
        else
            _topics.erase (topic);
        has_filters_after = !_topics.empty () || !_patterns.empty ();
    }
    if (_node) {
        _node->submit_sub_summary (this, has_filters_after
                                           ? ZLINK_TOPOLOGY_STATE_READY
                                           : ZLINK_TOPOLOGY_STATE_CONNECTING,
                                   0);
    }
    return 0;
}

int spot_sub_t::set_option (int option_,
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
        case ZLINK_SPOT_SUB_OPT_RCVHWM:
            socket_option = ZLINK_RCVHWM;
            break;
        case ZLINK_SPOT_SUB_OPT_RCVTIMEO:
            socket_option = ZLINK_RCVTIMEO;
            break;
        case ZLINK_SPOT_SUB_OPT_LINGER:
            socket_option = ZLINK_LINGER;
            break;
        case ZLINK_SPOT_SUB_OPT_SNDBUF:
            socket_option = ZLINK_SNDBUF;
            break;
        case ZLINK_SPOT_SUB_OPT_RCVBUF:
            socket_option = ZLINK_RCVBUF;
            break;
        case ZLINK_SPOT_SUB_OPT_QUEUE_NODROP:
        case ZLINK_SPOT_SUB_OPT_QUEUE_FULL_POLICY:
            errno = ENOTSUP;
            return -1;
        default:
            errno = EINVAL;
            return -1;
    }

    scoped_lock_t lock (_sync);
    return _socket->setsockopt (socket_option, optval_, optvallen_);
}

int spot_sub_t::set_routing_id (const void *data_, size_t size_)
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

int spot_sub_t::routing_id (zlink_routing_id_t *out_) const
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    *out_ = _routing_id;
    return 0;
}

int spot_sub_t::peers (zlink_peer_info_t *peers_, size_t *count_) const
{
    if (!_socket) {
        errno = EFAULT;
        return -1;
    }
    return zlink_socket_peers (static_cast<void *> (_socket), peers_, count_);
}

void *spot_sub_t::monitor_open (int events_)
{
    lock_routing_id ();
    if (ensure_monitor_bridge_started () != 0)
        return NULL;
    return _monitor.open (events_);
}

void *spot_sub_t::poller_socket ()
{
    lock_routing_id ();
    return static_cast<void *> (_socket);
}

bool spot_sub_t::has_filters () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    return !_topics.empty () || !_patterns.empty ();
}

int spot_sub_t::set_handler (zlink_spot_sub_handler_fn handler_,
                             void *userdata_)
{
    if (!_socket) {
        errno = EFAULT;
        return -1;
    }
    if (_node && _node->ensure_healthy () != 0)
        return -1;

    bool called_from_handler = false;
    {
        scoped_lock_t lock (_sync);
        if (_recv_in_progress.get () > 0) {
            errno = EBUSY;
            return -1;
        }

        if (handler_) {
            if (_handler_state != handler_none) {
                errno = EBUSY;
                return -1;
            }
            lock_routing_id ();
            _handler = handler_;
            _handler_userdata = userdata_;
            _handler_state = handler_active;
        } else {
            if (_handler_state == handler_none)
                return 0;
            _handler_state = handler_clearing;
            called_from_handler = current_handler_dispatch_sub == this;
        }
    }

    if (handler_) {
        if (_socket->sub_dispatch_start (&spot_sub_t::dispatch_from_io, this)
            == 0)
            return 0;

        scoped_lock_t lock (_sync);
        _handler = NULL;
        _handler_userdata = NULL;
        _handler_state = handler_none;
        return -1;
    }

    if (_socket->sub_dispatch_stop () != 0) {
        scoped_lock_t lock (_sync);
        _handler_state = handler_active;
        return -1;
    }

    {
        scoped_lock_t lock (_sync);
        while (!called_from_handler && _callback_inflight.get () > 0)
            _callback_cv.wait (&_sync, -1);
        _handler = NULL;
        _handler_userdata = NULL;
        _handler_state = handler_none;
        _callback_cv.broadcast ();
    }

    return 0;
}

int spot_sub_t::recv (zlink_msg_t **parts_,
                      size_t *part_count_,
                      int flags_,
                      char *topic_out_,
                      size_t *topic_len_)
{
    if (!parts_ || !part_count_) {
        errno = EINVAL;
        return -1;
    }
    if (!_node || !_socket) {
        errno = EFAULT;
        return -1;
    }
    if (_node->ensure_healthy () != 0)
        return -1;

    {
        scoped_lock_t lock (_sync);
        if (_handler_state == handler_active) {
            errno = EBUSY;
            return -1;
        }
        if (_recv_in_progress.get () > 0) {
            errno = EBUSY;
            return -1;
        }
        lock_routing_id ();
        _recv_in_progress.add (1);
    }

    *parts_ = NULL;
    *part_count_ = 0;
    std::vector<zlink_msg_t> frames;
    int rc = 0;
    while (true) {
        zlink_msg_t frame;
        zlink_msg_init (&frame);
        rc = _socket->recv (reinterpret_cast<msg_t *> (&frame),
                            frames.empty () ? flags_ : 0);
        if (rc != 0) {
            zlink_msg_close (&frame);
            close_msgv (&frames);
            break;
        }
        frames.push_back (frame);
        if (!zlink_msg_more (&frame))
            break;
    }

    {
        scoped_lock_t lock (_sync);
        _recv_in_progress.sub (1);
    }

    if (rc != 0)
        return -1;
    if (frames.empty()) {
        errno = EPROTO;
        return -1;
    }

    zlink_msg_t &topic = frames[0];
    const size_t topic_size = zlink_msg_size (&topic);
    if (topic_out_ && topic_len_) {
        if (*topic_len_ < topic_size) {
            close_msgv (&frames);
            errno = EMSGSIZE;
            return -1;
        }
        if (topic_size > 0)
            memcpy (topic_out_, zlink_msg_data (&topic), topic_size);
        *topic_len_ = topic_size;
    } else if (topic_out_) {
        if (topic_size > 0)
            memcpy (topic_out_, zlink_msg_data (&topic), topic_size);
        topic_out_[topic_size] = '\0';
    } else if (topic_len_) {
        *topic_len_ = topic_size;
    }

    const size_t payload_count = frames.size () - 1;
    if (payload_count == 0) {
        close_msgv (&frames);
        return 0;
    }

    zlink_msg_t *payload =
      static_cast<zlink_msg_t *> (malloc (payload_count * sizeof (zlink_msg_t)));
    if (!payload) {
        close_msgv (&frames);
        errno = ENOMEM;
        return -1;
    }
    memset (payload, 0, payload_count * sizeof (zlink_msg_t));

    for (size_t i = 0; i < payload_count; ++i) {
        msg_t *dst = reinterpret_cast<msg_t *> (&payload[i]);
        if (dst->init () != 0
            || dst->move (*reinterpret_cast<msg_t *> (&frames[i + 1])) != 0) {
            for (size_t j = 0; j <= i; ++j)
                zlink_msg_close (&payload[j]);
            free (payload);
            close_msgv (&frames);
            errno = EFAULT;
            return -1;
        }
    }

    zlink_msg_close (&frames[0]);
    *parts_ = payload;
    *part_count_ = payload_count;
    return 0;
}

void spot_sub_t::emit_ready_event ()
{
}

void spot_sub_t::dispatch_from_io (const char *topic_,
                                   size_t topic_len_,
                                   const zlink_msg_t *parts_,
                                   size_t part_count_,
                                   void *userdata_)
{
    spot_sub_t *self = static_cast<spot_sub_t *> (userdata_);
    if (!self)
        return;

    zlink_spot_sub_handler_fn handler = NULL;
    void *handler_userdata = NULL;
    {
        scoped_lock_t lock (self->_sync);
        if (self->_handler_state != handler_active || !self->_handler)
            return;
        handler = self->_handler;
        handler_userdata = self->_handler_userdata;
        self->_callback_inflight.add (1);
    }

    current_handler_dispatch_sub = self;
    handler (topic_, topic_len_, parts_, part_count_, handler_userdata);
    current_handler_dispatch_sub = NULL;

    {
        scoped_lock_t lock (self->_sync);
        self->_callback_inflight.sub (1);
        self->_callback_cv.broadcast ();
    }
}

void spot_sub_t::monitor_thread_main (void *arg_)
{
    static_cast<spot_sub_t *> (arg_)->monitor_loop ();
}

int spot_sub_t::ensure_monitor_bridge_started ()
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
    _monitor_thread.start (monitor_thread_main, this, "spot-sub-mon");
    _monitor_thread_started = true;
    return 0;
}

void spot_sub_t::stop_monitor_bridge ()
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

void spot_sub_t::monitor_loop ()
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
                break;

            default:
                break;
        }
    }
}

int spot_sub_t::destroy_internal (bool allow_embedded_default_,
                                  bool notify_node_)
{
    if (_node_owned_default && !allow_embedded_default_) {
        errno = EINVAL;
        return -1;
    }

    if (notify_node_ && _node)
        _node->submit_sub_summary (this, ZLINK_TOPOLOGY_STATE_STOPPED, 0);
    if (notify_node_ && _node)
        _node->remove_spot_sub (this);

    bool has_handler = false;
    {
        scoped_lock_t lock (_sync);
        has_handler = _handler_state != handler_none;
        if (has_handler)
            _handler_state = handler_clearing;
    }
    if (has_handler && _socket && _socket->sub_dispatch_active ())
        _socket->sub_dispatch_stop ();
    {
        scoped_lock_t lock (_sync);
        _handler = NULL;
        _handler_userdata = NULL;
        _handler_state = handler_none;
        _callback_cv.broadcast ();
    }
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
    _node_owned_default = false;
    return 0;
}

int spot_sub_t::destroy ()
{
    return destroy_internal (false, true);
}

int spot_sub_t::destroy_from_node ()
{
    return destroy_internal (true, true);
}

int spot_sub_t::abort_create ()
{
    return destroy_internal (true, false);
}
}
