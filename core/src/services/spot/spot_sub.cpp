/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_sub.hpp"
#include "services/common/monitor_decode.hpp"
#include "services/common/socket_monitor_bridge.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_runtime.hpp"

#include "sockets/socket_base.hpp"
#include "utils/err.hpp"
#include "utils/random.hpp"

#include <string.h>

namespace zlink
{
static const uint32_t spot_sub_tag_value = 0x1e6700da;

namespace
{
static void preserve_first_error (int rc_, int *first_error_)
{
    if (rc_ == 0 || !first_error_ || *first_error_ != 0)
        return;
    *first_error_ = errno != 0 ? errno : EIO;
}

static void close_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

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

static void copy_subject (char *dst_, size_t dst_size_, const char *src_)
{
    copy_endpoint (dst_, dst_size_, src_);
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

static void fill_subject_monitor_event (zlink_service_event_t *event_,
                                        uint32_t event_type_,
                                        const zlink_routing_id_t &rid_,
                                        const char *endpoint_,
                                        const char *subject_,
                                        uint32_t subject_kind_,
                                        uint32_t value_)
{
    memset (event_, 0, sizeof (*event_));
    event_->service_kind = ZLINK_SERVICE_KIND_SPOT_SUB;
    event_->event_type = event_type_;
    event_->routing_id = rid_;
    event_->value = value_;
    event_->detail_flags = ZLINK_EVENT_DETAIL_SUBJECT_RID;
    if (endpoint_ && endpoint_[0] != '\0') {
        copy_endpoint (event_->endpoint, sizeof (event_->endpoint), endpoint_);
        event_->detail_flags |= ZLINK_EVENT_DETAIL_ENDPOINT;
    }
    if (subject_ && subject_[0] != '\0') {
        copy_subject (event_->subject, sizeof (event_->subject), subject_);
        event_->detail_flags |= ZLINK_EVENT_DETAIL_SUBJECT;
    }
    if (subject_kind_ != ZLINK_SERVICE_EVENT_SUBJECT_NONE) {
        event_->subject_kind = subject_kind_;
        event_->detail_flags |= ZLINK_EVENT_DETAIL_SUBJECT_KIND;
    }
}

static std::string make_subject_key (const std::string &subject_,
                                     uint32_t subject_kind_)
{
    char prefix[16];
    snprintf (prefix, sizeof (prefix), "%u:", subject_kind_);
    return std::string (prefix) + subject_;
}

spot_sub_t::subject_descriptor_t::subject_descriptor_t () :
    subject_kind (ZLINK_SERVICE_EVENT_SUBJECT_NONE)
{
}

spot_sub_t::spot_sub_t (spot_node_t *node_,
                        uint64_t attachment_id_,
                        bool node_owned_default_) :
    _node (node_),
    _attachment_id (attachment_id_),
    _tag (spot_sub_tag_value),
    _node_owned_default (node_owned_default_),
    _routing_id_locked (false),
    _direct_handler (NULL),
    _direct_handler_userdata (NULL),
    _handler_state (handler_none),
    _callback_inflight (0),
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

socket_base_t *spot_sub_t::socket () const
{
    if (!_node || !_node->_runtime)
        return NULL;
    return _node->_runtime->attachment_socket (_attachment_id);
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
    socket_base_t *socket = this->socket ();
    if (!_node || !socket) {
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
        if (socket->setsockopt (ZLINK_SUBSCRIBE, topic.data (), topic.size ())
            != 0)
            return -1;
        _topics.insert (topic);
    }
    if (_node) {
        _node->submit_sub_summary (this, ZLINK_TOPOLOGY_STATE_READY, 0);
        emit_filter_applied_event (topic.c_str (),
                                   ZLINK_SERVICE_EVENT_SUBJECT_TOPIC);
    }
    return 0;
}

int spot_sub_t::subscribe_pattern (const char *pattern_)
{
    socket_base_t *socket = this->socket ();
    if (!_node || !socket) {
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
        if (socket->setsockopt (ZLINK_SUBSCRIBE, prefix.data (), prefix.size ())
            != 0)
            return -1;
        _patterns.insert (prefix);
    }
    if (_node) {
        _node->submit_sub_summary (this, ZLINK_TOPOLOGY_STATE_READY, 0);
        std::string subject = prefix + "*";
        emit_filter_applied_event (subject.c_str (),
                                   ZLINK_SERVICE_EVENT_SUBJECT_PATTERN);
    }
    return 0;
}

int spot_sub_t::unsubscribe (const char *topic_or_pattern_)
{
    socket_base_t *socket = this->socket ();
    if (!_node || !socket) {
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
    subject_descriptor_t subject;
    subject.subject_kind = is_pattern ? ZLINK_SERVICE_EVENT_SUBJECT_PATTERN
                                      : ZLINK_SERVICE_EVENT_SUBJECT_TOPIC;
    subject.subject = is_pattern ? prefix + "*" : topic;
    {
        scoped_lock_t lock (_sync);
        lock_routing_id ();
        const std::string &filter = is_pattern ? prefix : topic;
        if (socket->setsockopt (ZLINK_UNSUBSCRIBE, filter.data (), filter.size ())
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
    mark_subject_lost (subject, NULL);
    return 0;
}

int spot_sub_t::set_option (int option_,
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
        case ZLINK_SPOT_SUB_OPT_RCVHWM:
            socket_option = ZLINK_RCVHWM;
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
        default:
            errno = EINVAL;
            return -1;
    }

    scoped_lock_t lock (_sync);
    return socket->setsockopt (socket_option, optval_, optvallen_);
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
    socket_base_t *socket = this->socket ();
    if (!socket) {
        errno = EFAULT;
        return -1;
    }
    return zlink_socket_peers (static_cast<void *> (socket), peers_, count_);
}

void *spot_sub_t::monitor_open (int events_)
{
    lock_routing_id ();
    if (ensure_monitor_bridge_started () != 0)
        return NULL;
    return _monitor.open (events_);
}

bool spot_sub_t::has_filters () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    return !_topics.empty () || !_patterns.empty ();
}

void spot_sub_t::append_raw_filters (std::set<std::string> *out_) const
{
    if (!out_)
        return;

    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    out_->insert (_topics.begin (), _topics.end ());
    out_->insert (_patterns.begin (), _patterns.end ());
}

void spot_sub_t::append_all_subjects (
  std::vector<subject_descriptor_t> *out_) const
{
    if (!out_)
        return;

    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    for (std::set<std::string>::const_iterator it = _topics.begin ();
         it != _topics.end (); ++it) {
        subject_descriptor_t subject;
        subject.subject = *it;
        subject.subject_kind = ZLINK_SERVICE_EVENT_SUBJECT_TOPIC;
        out_->push_back (subject);
    }
    for (std::set<std::string>::const_iterator it = _patterns.begin ();
         it != _patterns.end (); ++it) {
        subject_descriptor_t subject;
        subject.subject = *it + "*";
        subject.subject_kind = ZLINK_SERVICE_EVENT_SUBJECT_PATTERN;
        out_->push_back (subject);
    }
}

void spot_sub_t::append_subjects_for_raw_filter (
  const std::string &raw_filter_,
  std::vector<subject_descriptor_t> *out_) const
{
    if (!out_ || raw_filter_.empty ())
        return;

    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    if (_topics.count (raw_filter_) != 0) {
        subject_descriptor_t subject;
        subject.subject = raw_filter_;
        subject.subject_kind = ZLINK_SERVICE_EVENT_SUBJECT_TOPIC;
        out_->push_back (subject);
    }
    if (_patterns.count (raw_filter_) != 0) {
        subject_descriptor_t subject;
        subject.subject = raw_filter_ + "*";
        subject.subject_kind = ZLINK_SERVICE_EVENT_SUBJECT_PATTERN;
        out_->push_back (subject);
    }
}

void spot_sub_t::emit_filter_applied_event (const char *subject_,
                                            uint32_t subject_kind_)
{
    zlink_service_event_t event;
    {
        scoped_lock_t lock (_sync);
        fill_subject_monitor_event (&event, ZLINK_SPOT_SUB_FILTER_APPLIED,
                                    _routing_id, NULL, subject_,
                                    subject_kind_, 0);
    }
    _monitor.emit (event);
}

void spot_sub_t::emit_subscription_ready_event (const char *endpoint_,
                                                const char *subject_,
                                                uint32_t subject_kind_)
{
    zlink_service_event_t event;
    {
        scoped_lock_t lock (_sync);
        fill_subject_monitor_event (&event, ZLINK_SPOT_SUB_SUBSCRIPTION_READY,
                                    _routing_id, endpoint_, subject_,
                                    subject_kind_, 1);
    }
    _monitor.emit (event);
}

void spot_sub_t::emit_delivery_ready_changed_event (const char *subject_,
                                                    uint32_t subject_kind_,
                                                    uint32_t ready_,
                                                    const char *endpoint_)
{
    zlink_service_event_t event;
    {
        scoped_lock_t lock (_sync);
        fill_subject_monitor_event (&event,
                                    ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED,
                                    _routing_id, endpoint_, subject_,
                                    subject_kind_, ready_);
    }
    _monitor.emit (event);
}

std::string spot_sub_t::first_ready_peer_endpoint () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    if (_ready_peer_endpoints.empty ())
        return std::string ();
    return *_ready_peer_endpoints.begin ();
}

int spot_sub_t::set_direct_handler (spot_sub_direct_handler_fn handler_,
                                    void *userdata_)
{
    socket_base_t *socket = this->socket ();
    if (!socket) {
        errno = EFAULT;
        return -1;
    }
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }
    if (_node && _node->ensure_healthy () != 0)
        return -1;
    {
        scoped_lock_t lock (_sync);
        if (_handler_state == handler_active) {
            _direct_handler = handler_;
            _direct_handler_userdata = userdata_;
            return 0;
        }
        if (_handler_state != handler_none) {
            errno = EBUSY;
            return -1;
        }
        _direct_handler = handler_;
        _direct_handler_userdata = userdata_;
        _handler_state = handler_active;
    }

    if (socket->sub_dispatch_start (&spot_sub_t::dispatch_from_io, this) == 0)
        return 0;

    {
        scoped_lock_t lock (_sync);
        _direct_handler = NULL;
        _direct_handler_userdata = NULL;
        _handler_state = handler_none;
    }
    return -1;
}

void spot_sub_t::emit_ready_event ()
{
}

void spot_sub_t::mark_subject_ready (const subject_descriptor_t &subject_,
                                     const char *endpoint_)
{
    if (subject_.subject.empty ()
        || subject_.subject_kind == ZLINK_SERVICE_EVENT_SUBJECT_NONE)
        return;

    bool inserted = false;
    {
        scoped_lock_t lock (_sync);
        inserted = _ready_subject_keys.insert (
                     make_subject_key (subject_.subject, subject_.subject_kind))
                     .second;
    }
    if (!inserted)
        return;

    emit_subscription_ready_event (endpoint_, subject_.subject.c_str (),
                                   subject_.subject_kind);
    emit_delivery_ready_changed_event (subject_.subject.c_str (),
                                       subject_.subject_kind, 1, endpoint_);
}

void spot_sub_t::mark_subject_lost (const subject_descriptor_t &subject_,
                                    const char *endpoint_)
{
    if (subject_.subject.empty ()
        || subject_.subject_kind == ZLINK_SERVICE_EVENT_SUBJECT_NONE)
        return;

    bool erased = false;
    {
        scoped_lock_t lock (_sync);
        erased =
          _ready_subject_keys.erase (
            make_subject_key (subject_.subject, subject_.subject_kind))
          != 0;
    }
    if (!erased)
        return;

    emit_delivery_ready_changed_event (subject_.subject.c_str (),
                                       subject_.subject_kind, 0, endpoint_);
}

void spot_sub_t::mark_all_subjects_lost (const char *endpoint_)
{
    std::vector<subject_descriptor_t> subjects;
    append_all_subjects (&subjects);
    for (size_t i = 0; i < subjects.size (); ++i)
        mark_subject_lost (subjects[i], endpoint_);
}

void spot_sub_t::dispatch_from_io (const zlink_routing_id_t *source_rid_,
                                   const char *topic_,
                                   size_t topic_len_,
                                   zlink_msg_t *parts_,
                                   size_t part_count_,
                                   void *userdata_)
{
    spot_sub_t *self = static_cast<spot_sub_t *> (userdata_);
    if (!self) {
        close_parts (parts_, part_count_);
        return;
    }

    spot_sub_direct_handler_fn direct_handler = NULL;
    void *direct_handler_userdata = NULL;
    {
        scoped_lock_t lock (self->_sync);
        if (self->_handler_state != handler_active || !self->_direct_handler) {
            close_parts (parts_, part_count_);
            return;
        }
        direct_handler = self->_direct_handler;
        direct_handler_userdata = self->_direct_handler_userdata;
        self->_callback_inflight.add (1);
    }

    direct_handler (source_rid_, topic_, topic_len_, parts_, part_count_,
                    direct_handler_userdata);

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
    _monitor_thread.start (monitor_thread_main, this, "spot-sub-mon");
    _monitor_thread_started = true;
    return 0;
}

int spot_sub_t::stop_monitor_bridge ()
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

            case ZLINK_EVENT_CONNECTION_READY: {
                {
                    scoped_lock_t lock (_sync);
                    if (raw.remote_addr[0] != '\0')
                        _ready_peer_endpoints.insert (raw.remote_addr);
                }
                fill_socket_monitor_event (&event, ZLINK_MONITOR_EVENT_READY,
                                           raw);
                _monitor.emit (event);
                fill_socket_monitor_event (&event, ZLINK_MONITOR_EVENT_PEER_UP,
                                           raw);
                _monitor.emit (event);
                break;
            }

            case ZLINK_EVENT_DISCONNECTED:
                {
                    scoped_lock_t lock (_sync);
                    if (raw.remote_addr[0] != '\0')
                        _ready_peer_endpoints.erase (raw.remote_addr);
                }
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

    socket_base_t *socket = this->socket ();
    int first_error = 0;

    if (notify_node_ && _node)
        _node->remove_spot_sub (this);
    if (notify_node_ && _node)
        _node->submit_sub_summary (this, ZLINK_TOPOLOGY_STATE_STOPPED, 0);

    bool has_handler = false;
    {
        scoped_lock_t lock (_sync);
        has_handler = _handler_state != handler_none;
        if (has_handler)
            _handler_state = handler_clearing;
    }

    if (socket) {
        for (std::set<std::string>::const_iterator it = _topics.begin (),
                                                   end = _topics.end ();
             it != end; ++it)
            (void) socket->setsockopt (ZLINK_UNSUBSCRIBE, it->c_str (),
                                        it->size ());
        for (std::set<std::string>::const_iterator it = _patterns.begin (),
                                                   end = _patterns.end ();
             it != end; ++it)
            (void) socket->setsockopt (ZLINK_UNSUBSCRIBE, it->c_str (),
                                        it->size ());
    }
    if (has_handler && socket && socket->sub_dispatch_active ())
        socket->sub_dispatch_stop ();
    {
        scoped_lock_t lock (_sync);
        while (_callback_inflight.get () > 0)
            (void) _callback_cv.wait (&_sync, 1000);
        _topics.clear ();
        _patterns.clear ();
        _ready_peer_endpoints.clear ();
        _ready_subject_keys.clear ();
        _direct_handler = NULL;
        _direct_handler_userdata = NULL;
        _handler_state = handler_none;
        _callback_cv.broadcast ();
    }
    preserve_first_error (stop_monitor_bridge (), &first_error);

    zlink_service_event_t terminal;
    fill_terminal_monitor_event (&terminal, ZLINK_MONITOR_EVENT_CLOSED,
                                 _routing_id);
    _monitor.close_all (&terminal);

    if (socket) {
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
