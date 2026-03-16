/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_sub.hpp"
#include "services/common/monitor_decode.hpp"
#include "services/common/socket_monitor_bridge.hpp"
#include "services/spot/spot_control_protocol.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_runtime.hpp"

#include "sockets/socket_base.hpp"
#include "utils/err.hpp"
#include "utils/random.hpp"

#include <stdio.h>
#include <string.h>

namespace zlink
{
static const uint32_t spot_sub_tag_value = 0x1e6700da;
static const char spot_ready_probe_prefix[] = "__zlink.ready__/";
static const char spot_ready_probe_marker[] =
  "\x00zlink.ready.probe.v1\x00";

namespace
{
static std::string routing_id_to_hex (const zlink_routing_id_t &rid_)
{
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve (static_cast<size_t> (rid_.size) * 2);
    for (size_t i = 0; i < rid_.size; ++i) {
        const unsigned char byte = rid_.data[i];
        out.push_back (hex[(byte >> 4) & 0x0f]);
        out.push_back (hex[byte & 0x0f]);
    }
    return out;
}

static void preserve_first_error (int rc_, int *first_error_)
{
    if (rc_ == 0 || !first_error_ || *first_error_ != 0)
        return;
    *first_error_ = errno != 0 ? errno : EIO;
}

static void emit_monitor_event_batch (service_monitor_hub_t *monitor_,
                                      zlink_service_event_t *events_,
                                      size_t count_)
{
    if (!monitor_ || !events_ || count_ == 0)
        return;
    monitor_->emit_batch (events_, count_);
}

static void close_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

static void close_msgv (std::vector<zlink_msg_t> *parts_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < parts_->size (); ++i)
        zlink_msg_close (&(*parts_)[i]);
    parts_->clear ();
}


static bool is_ready_probe_message (const char *topic_,
                                    size_t topic_len_,
                                    zlink_msg_t *parts_,
                                    size_t part_count_,
                                    std::string *raw_filter_out_,
                                    std::string *peer_endpoint_out_)
{
    if (!topic_ || topic_len_ == 0 || !parts_ || part_count_ != 2)
        return false;
    if (zlink_msg_size (&parts_[0]) != sizeof (spot_ready_probe_marker) - 1)
        return false;
    if (memcmp (zlink_msg_data (&parts_[0]), spot_ready_probe_marker,
                sizeof (spot_ready_probe_marker) - 1)
        != 0) {
        return false;
    }
    if (zlink_msg_size (&parts_[1]) == 0)
        return false;

    if (raw_filter_out_)
        raw_filter_out_->assign (topic_, topic_len_);
    if (peer_endpoint_out_) {
        peer_endpoint_out_->assign (
          static_cast<const char *> (zlink_msg_data (&parts_[1])),
          zlink_msg_size (&parts_[1]));
    }
    return true;
}

static bool may_be_ready_probe_topic (const char *topic_, size_t topic_len_)
{
    return topic_ && topic_len_ >= sizeof (spot_ready_probe_prefix) - 1
           && memcmp (topic_, spot_ready_probe_prefix,
                      sizeof (spot_ready_probe_prefix) - 1)
                == 0;
}

static void spot_sub_diag_log (const char *stage_)
{
    if (!getenv ("ZLINK_SPOT_SUB_DIAG_LOG"))
        return;

    FILE *fp = fopen ("/tmp/zlink_spot_sub_diag.log", "a");
    if (!fp)
        return;

    fprintf (fp,
             "ts=%llu pid=%ld stage=%s\n",
             static_cast<unsigned long long> (zlink::clock_t ().now_ms ()),
             static_cast<long> (getpid ()),
             stage_ ? stage_ : "?");
    fclose (fp);
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
                        socket_base_t *socket_,
                        uint64_t attachment_id_,
                        bool node_owned_default_) :
    _node (node_),
    _socket (socket_),
    _runtime (node_ ? node_->runtime () : NULL),
    _attachment_id (attachment_id_),
    _tag (spot_sub_tag_value),
    _node_owned_default (node_owned_default_),
    _routing_id_locked (false),
    _direct_handler_binding_index (0),
    _active_direct_handler (NULL),
    _handler_state (handler_none),
    _callback_inflight (0),
    _destroying (false),
    _monitor (node_ ? node_->ctx () : NULL),
    _monitor_event_queue (),
    _monitor_event_draining (false),
    _monitor_event_pending (0),
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

void spot_sub_t::emit_monitor_event (const zlink_service_event_t &event_)
{
    if (_destroying.load (std::memory_order_acquire))
        return;
    _monitor.emit (event_);
}

socket_base_t *spot_sub_t::socket () const
{
    return _socket;
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
    const std::string value (topic_, len);
    if (spot_control_protocol::is_reserved_subject (value))
        return false;
    if (out_)
        *out_ = value;
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
    const std::string prefix (pattern_, len - 1);
    if (spot_control_protocol::is_reserved_subject (prefix))
        return false;
    if (prefix_out_)
        *prefix_out_ = prefix;
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

    bool had_filters = false;
    bool has_filters = false;
    {
        scoped_lock_t lock (_sync);
        had_filters = !_topics.empty () || !_patterns.empty ();
        lock_routing_id ();
        if (socket->setsockopt (ZLINK_SUBSCRIBE, topic.data (), topic.size ())
            != 0)
            return -1;
        _topics.insert (topic);
        _delivery_ready_raw_filters.erase (topic);
        has_filters = !_topics.empty () || !_patterns.empty ();
    }
    if (_node) {
        _node->note_local_sub_filters_changed (had_filters, has_filters);
        emit_filter_applied_event (topic.c_str (),
                                   ZLINK_SERVICE_EVENT_SUBJECT_TOPIC);
        if (_node->send_subscription_update (topic, true) != 0)
            return -1;
        if (_node->has_active_peers ())
            _node->notify_subscription_forwarded (topic);
        _node->schedule_subscription_replay ();
        if (_node->replay_subscriptions_if_active_peers () != 0)
            return -1;
        _node->submit_sub_summary (this, ZLINK_TOPOLOGY_STATE_READY, 0);
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

    bool had_filters = false;
    bool has_filters = false;
    {
        scoped_lock_t lock (_sync);
        had_filters = !_topics.empty () || !_patterns.empty ();
        lock_routing_id ();
        if (socket->setsockopt (ZLINK_SUBSCRIBE, prefix.data (), prefix.size ())
            != 0)
            return -1;
        _patterns.insert (prefix);
        _delivery_ready_raw_filters.erase (prefix);
        has_filters = !_topics.empty () || !_patterns.empty ();
    }
    if (_node) {
        _node->note_local_sub_filters_changed (had_filters, has_filters);
        std::string subject = prefix + "*";
        emit_filter_applied_event (subject.c_str (),
                                   ZLINK_SERVICE_EVENT_SUBJECT_PATTERN);
        if (_node->send_subscription_update (prefix, true) != 0)
            return -1;
        if (_node->has_active_peers ())
            _node->notify_subscription_forwarded (prefix);
        _node->schedule_subscription_replay ();
        if (_node->replay_subscriptions_if_active_peers () != 0)
            return -1;
        _node->submit_sub_summary (this, ZLINK_TOPOLOGY_STATE_READY, 0);
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

    bool had_filters = false;
    bool has_filters_after = false;
    int first_error = 0;
    std::vector<std::string> ready_ack_endpoints;
    subject_descriptor_t subject;
    subject.subject_kind = is_pattern ? ZLINK_SERVICE_EVENT_SUBJECT_PATTERN
                                      : ZLINK_SERVICE_EVENT_SUBJECT_TOPIC;
    subject.subject = is_pattern ? prefix + "*" : topic;
    const std::string filter = is_pattern ? prefix : topic;
    {
        scoped_lock_t lock (_sync);
        had_filters = !_topics.empty () || !_patterns.empty ();
        lock_routing_id ();
        if (socket->setsockopt (ZLINK_UNSUBSCRIBE, filter.data (), filter.size ())
            != 0)
            return -1;
        if (is_pattern)
            _patterns.erase (prefix);
        else
            _topics.erase (topic);
        _delivery_ready_raw_filters.erase (filter);
        has_filters_after = !_topics.empty () || !_patterns.empty ();
    }
    if (_node)
        _node->note_local_sub_filters_changed (had_filters, has_filters_after);
    if (_node && _node->send_subscription_update (filter, false) != 0)
        first_error = errno != 0 ? errno : EIO;
    release_ready_ack_endpoints (filter, &ready_ack_endpoints);
    if (_node) {
        const std::string ack_source_id = ready_ack_source_id ();
        for (size_t i = 0; i < ready_ack_endpoints.size (); ++i) {
            (void) _node->send_ready_ack_update (ready_ack_endpoints[i], filter,
                                                 ack_source_id, false);
        }
    }
    if (_node) {
        _node->submit_sub_summary (this, has_filters_after
                                           ? ZLINK_TOPOLOGY_STATE_READY
                                           : ZLINK_TOPOLOGY_STATE_CONNECTING,
                                   0);
    }
    mark_subject_lost (subject, NULL);
    if (first_error != 0) {
        errno = first_error;
        return -1;
    }
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
        case ZLINK_SPOT_SUB_OPT_RCVTIMEO:
            socket_option = ZLINK_RCVTIMEO;
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

std::string spot_sub_t::ready_ack_source_id () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    return routing_id_to_hex (_routing_id);
}

int spot_sub_t::fill_monitor_snapshot (zlink_monitor_snapshot_t *out_) const
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }
    socket_base_t *socket = this->socket ();
    if (!socket) {
        errno = EFAULT;
        return -1;
    }
    if (socket->monitor_snapshot (out_) != 0)
        return -1;
    out_->source_kind = ZLINK_MONITOR_SOURCE_SPOT_SUB;
    {
        scoped_lock_t lock (const_cast<mutex_t &> (_sync));
        const uint32_t ready_peer_count =
          static_cast<uint32_t> (_ready_peer_endpoints.size ());
        if (out_->ready_peer_count < ready_peer_count)
            out_->ready_peer_count = ready_peer_count;
    }
    out_->detail_flags |= ZLINK_MONITOR_SNAPSHOT_DETAIL_READY_PEER_COUNT;
    if (out_->ready_peer_count > 0)
        out_->state_flags |= ZLINK_MONITOR_STATE_READY;
    else
        out_->state_flags &= ~ZLINK_MONITOR_STATE_READY;
    return 0;
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

void spot_sub_t::append_replay_raw_filters (std::set<std::string> *out_) const
{
    if (!out_)
        return;

    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    for (std::set<std::string>::const_iterator it = _topics.begin ();
         it != _topics.end (); ++it) {
        if (_delivery_ready_raw_filters.count (*it) == 0)
            out_->insert (*it);
    }
    for (std::set<std::string>::const_iterator it = _patterns.begin ();
         it != _patterns.end (); ++it) {
        if (_delivery_ready_raw_filters.count (*it) == 0)
            out_->insert (*it);
    }
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
    emit_monitor_event (event);
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
    emit_monitor_event (event);
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
    emit_monitor_event (event);
}

void spot_sub_t::mark_subject_subscription_ready (
  const subject_descriptor_t &subject_,
  const char *endpoint_)
{
    if (subject_.subject.empty ()
        || subject_.subject_kind == ZLINK_SERVICE_EVENT_SUBJECT_NONE)
        return;

    emit_subscription_ready_event (endpoint_, subject_.subject.c_str (),
                                   subject_.subject_kind);
    mark_subject_ready (subject_, endpoint_);

    if (!endpoint_ || endpoint_[0] == '\0' || !_node)
        return;

    std::string raw_filter = subject_.subject;
    if (subject_.subject_kind == ZLINK_SERVICE_EVENT_SUBJECT_PATTERN
        && !raw_filter.empty ()
        && raw_filter[raw_filter.size () - 1] == '*') {
        raw_filter.erase (raw_filter.size () - 1);
    }
    if (raw_filter.empty ())
        return;

    bool already_acked = false;
    {
        scoped_lock_t lock (_sync);
        std::map<std::string, std::set<std::string> >::const_iterator it =
          _ready_ack_endpoints.find (raw_filter);
        already_acked =
          it != _ready_ack_endpoints.end ()
          && it->second.count (endpoint_) != 0;
    }
    if (already_acked)
        return;

    if (_node->send_ready_ack_update (endpoint_, raw_filter,
                                      ready_ack_source_id (), true)
        != 0)
        return;

    {
        scoped_lock_t lock (_sync);
        _ready_ack_endpoints[raw_filter].insert (endpoint_);
    }
}

std::string spot_sub_t::first_ready_peer_endpoint () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    if (_ready_peer_endpoints.empty ())
        return std::string ();
    return *_ready_peer_endpoints.begin ();
}

void spot_sub_t::send_ready_ack_lost_for_endpoint (const char *endpoint_)
{
    if (!endpoint_ || endpoint_[0] == '\0' || !_node)
        return;

    const std::string endpoint (endpoint_);
    std::vector<std::string> raw_filters;
    {
        scoped_lock_t lock (_sync);
        for (std::map<std::string, std::set<std::string> >::const_iterator it =
               _ready_ack_endpoints.begin ();
             it != _ready_ack_endpoints.end (); ++it) {
            if (it->second.count (endpoint) != 0)
                raw_filters.push_back (it->first);
        }
    }

    if (raw_filters.empty ())
        return;

    const std::string ack_source_id = ready_ack_source_id ();
    for (size_t i = 0; i < raw_filters.size (); ++i)
        (void) _node->send_ready_ack_update (endpoint, raw_filters[i],
                                             ack_source_id, false);
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
        if (_handler_state.load (std::memory_order_acquire) == handler_active) {
            const unsigned int next_binding =
              (_direct_handler_binding_index + 1) % 2;
            _direct_handler_bindings[next_binding].handler = handler_;
            _direct_handler_bindings[next_binding].userdata = userdata_;
            _active_direct_handler.store (&_direct_handler_bindings[next_binding],
                                          std::memory_order_release);
            _direct_handler_binding_index = next_binding;
            return 0;
        }
        if (_handler_state.load (std::memory_order_acquire) != handler_none) {
            errno = EBUSY;
            return -1;
        }
        _direct_handler_bindings[0].handler = handler_;
        _direct_handler_bindings[0].userdata = userdata_;
        _direct_handler_binding_index = 0;
        _active_direct_handler.store (&_direct_handler_bindings[0],
                                      std::memory_order_release);
        _handler_state.store (handler_active, std::memory_order_release);
    }

    if (socket->sub_dispatch_start (&spot_sub_t::dispatch_from_io, this) == 0) {
        return 0;
    }

    {
        scoped_lock_t lock (_sync);
        _active_direct_handler.store (NULL, std::memory_order_release);
        _handler_state.store (handler_none, std::memory_order_release);
    }
    return -1;
}

int spot_sub_t::recv (zlink_msg_t **parts_,
                      size_t *part_count_,
                      int flags_,
                      char *topic_out_,
                      size_t *topic_len_)
{
    socket_base_t *socket = this->socket ();
    if (!parts_ || !part_count_) {
        errno = EINVAL;
        return -1;
    }
    if (!_node || !socket) {
        errno = EFAULT;
        return -1;
    }
    if (!_runtime || _runtime->ensure_healthy () != 0)
        return -1;

    {
        scoped_lock_t lock (_sync);
        if (_handler_state.load (std::memory_order_acquire) != handler_none) {
            errno = EBUSY;
            return -1;
        }
        lock_routing_id ();
    }

    while (true) {
        *parts_ = NULL;
        *part_count_ = 0;

        std::vector<zlink_msg_t> frames;
        frames.reserve (2);

        int rc = 0;
        zlink_msg_t topic_frame;
        zlink_msg_init (&topic_frame);
        rc = socket->recv (reinterpret_cast<msg_t *> (&topic_frame), flags_);
        if (rc != 0) {
            zlink_msg_close (&topic_frame);
            return -1;
        }
        frames.push_back (topic_frame);

        if (zlink_msg_more (&topic_frame)) {
            zlink_msg_t first_payload_frame;
            zlink_msg_init (&first_payload_frame);
            rc = socket->recv (reinterpret_cast<msg_t *> (&first_payload_frame), 0);
            if (rc != 0) {
                zlink_msg_close (&first_payload_frame);
                close_msgv (&frames);
                return -1;
            }

            if (!zlink_msg_more (&first_payload_frame)) {
                const char *topic_data = static_cast<const char *> (
                  zlink_msg_data (&frames[0]));
                const size_t topic_size = zlink_msg_size (&frames[0]);

                if (topic_out_ && topic_len_) {
                    if (*topic_len_ < topic_size) {
                        zlink_msg_close (&first_payload_frame);
                        close_msgv (&frames);
                        errno = EMSGSIZE;
                        return -1;
                    }
                    if (topic_size > 0)
                        memcpy (topic_out_, topic_data, topic_size);
                    *topic_len_ = topic_size;
                } else if (topic_out_) {
                    if (topic_size > 0)
                        memcpy (topic_out_, topic_data, topic_size);
                    topic_out_[topic_size] = '\0';
                } else if (topic_len_) {
                    *topic_len_ = topic_size;
                }

                zlink_msg_t *payload =
                  static_cast<zlink_msg_t *> (malloc (sizeof (zlink_msg_t)));
                if (!payload) {
                    zlink_msg_close (&first_payload_frame);
                    close_msgv (&frames);
                    errno = ENOMEM;
                    return -1;
                }
                memset (payload, 0, sizeof (zlink_msg_t));

                msg_t *dst = reinterpret_cast<msg_t *> (payload);
                if (dst->init () != 0
                    || dst->move (
                         *reinterpret_cast<msg_t *> (&first_payload_frame))
                         != 0) {
                    zlink_msg_close (&first_payload_frame);
                    zlink_msg_close (payload);
                    free (payload);
                    close_msgv (&frames);
                    errno = EFAULT;
                    return -1;
                }

                zlink_msg_close (&frames[0]);
                zlink_msg_close (&first_payload_frame);
                *parts_ = payload;
                *part_count_ = 1;
                return 0;
            }

            frames.push_back (first_payload_frame);
            while (true) {
                zlink_msg_t frame;
                zlink_msg_init (&frame);
                rc = socket->recv (reinterpret_cast<msg_t *> (&frame), 0);
                if (rc != 0) {
                    zlink_msg_close (&frame);
                    close_msgv (&frames);
                    return -1;
                }
                frames.push_back (frame);
                if (!zlink_msg_more (&frame))
                    break;
            }
        }

        if (rc != 0)
            return -1;
        if (frames.empty ()) {
            errno = EPROTO;
            return -1;
        }

        zlink_msg_t &topic = frames[0];
        const char *topic_data =
          static_cast<const char *> (zlink_msg_data (&topic));
        const size_t topic_size = zlink_msg_size (&topic);

        if (may_be_ready_probe_topic (topic_data, topic_size)) {
            std::string raw_filter;
            std::string peer_endpoint;
            if (is_ready_probe_message (topic_data,
                                        topic_size,
                                        frames.size () > 1 ? &frames[1] : NULL,
                                        frames.size () - 1,
                                        &raw_filter,
                                        &peer_endpoint)) {
                close_msgv (&frames);
                handle_ready_probe (raw_filter, peer_endpoint);
                continue;
            }
        }

        if (topic_out_ && topic_len_) {
            if (*topic_len_ < topic_size) {
                close_msgv (&frames);
                errno = EMSGSIZE;
                return -1;
            }
            if (topic_size > 0)
                memcpy (topic_out_, topic_data, topic_size);
            *topic_len_ = topic_size;
        } else if (topic_out_) {
            if (topic_size > 0)
                memcpy (topic_out_, topic_data, topic_size);
            topic_out_[topic_size] = '\0';
        } else if (topic_len_) {
            *topic_len_ = topic_size;
        }

        const size_t payload_count = frames.size () - 1;
        if (payload_count == 0) {
            close_msgv (&frames);
            return 0;
        }

        zlink_msg_t *payload = static_cast<zlink_msg_t *> (
          malloc (payload_count * sizeof (zlink_msg_t)));
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

    bool emit = false;
    std::string endpoint_value;
    {
        scoped_lock_t lock (_sync);
        const std::string key =
          make_subject_key (subject_.subject, subject_.subject_kind);
        std::map<std::string, std::string>::iterator it =
          _ready_subject_endpoints.find (key);
        const std::string new_endpoint =
          endpoint_ && endpoint_[0] != '\0' ? std::string (endpoint_)
                                            : std::string ();
        if (it == _ready_subject_endpoints.end ()) {
            _ready_subject_endpoints[key] = new_endpoint;
            endpoint_value = new_endpoint;
            emit = true;
        } else if (it->second.empty () && !new_endpoint.empty ()) {
            it->second = new_endpoint;
            endpoint_value = new_endpoint;
            emit = true;
        }
    }
    if (!emit)
        return;

    emit_delivery_ready_changed_event (subject_.subject.c_str (),
                                       subject_.subject_kind, 1,
                                       endpoint_value.empty ()
                                         ? NULL
                                         : endpoint_value.c_str ());
}

void spot_sub_t::backfill_subject_ready_endpoint (
  const subject_descriptor_t &subject_,
  const char *endpoint_)
{
    if (subject_.subject.empty ()
        || subject_.subject_kind == ZLINK_SERVICE_EVENT_SUBJECT_NONE
        || !endpoint_ || endpoint_[0] == '\0')
        return;

    bool emit = false;
    {
        scoped_lock_t lock (_sync);
        const std::string key =
          make_subject_key (subject_.subject, subject_.subject_kind);
        std::map<std::string, std::string>::iterator it =
          _ready_subject_endpoints.find (key);
        if (getenv ("ZLINK_DEBUG_SPOT_BACKFILL")) {
            fprintf (stderr,
                     "[spot-backfill] subject=%s kind=%u endpoint=%s found=%d "
                     "stored=%s\n",
                     subject_.subject.c_str (),
                     static_cast<unsigned int> (subject_.subject_kind),
                     endpoint_,
                     it != _ready_subject_endpoints.end () ? 1 : 0,
                     (it != _ready_subject_endpoints.end ()
                      && !it->second.empty ())
                       ? it->second.c_str ()
                       : "-");
        }
        if (it != _ready_subject_endpoints.end () && it->second.empty ()) {
            it->second = endpoint_;
            emit = true;
        }
    }
    if (!emit)
        return;

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
          _ready_subject_endpoints.erase (
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
    std::vector<std::pair<std::string, std::string> > ready_ack_updates;
    {
        scoped_lock_t lock (_sync);
        _delivery_ready_raw_filters.clear ();
    }
    append_all_subjects (&subjects);
    release_all_ready_ack_endpoints (&ready_ack_updates);
    if (_node) {
        const std::string ack_source_id = ready_ack_source_id ();
        for (size_t i = 0; i < ready_ack_updates.size (); ++i) {
            (void) _node->send_ready_ack_update (ready_ack_updates[i].second,
                                                 ready_ack_updates[i].first,
                                                 ack_source_id, false);
        }
    }
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

    if (may_be_ready_probe_topic (topic_, topic_len_)) {
        std::string raw_filter;
        std::string peer_endpoint;
        if (is_ready_probe_message (topic_, topic_len_, parts_, part_count_,
                                    &raw_filter, &peer_endpoint)) {
            self->handle_ready_probe (raw_filter, peer_endpoint);
            close_parts (parts_, part_count_);
            return;
        }
    }

    direct_handler_binding_t *binding =
      self->_active_direct_handler.load (std::memory_order_acquire);
    if (self->_handler_state.load (std::memory_order_acquire) != handler_active
        || !binding || !binding->handler) {
        close_parts (parts_, part_count_);
        return;
    }

    self->_callback_inflight.add (1);
    binding->handler (source_rid_, topic_, topic_len_, parts_, part_count_,
                      binding->userdata);

    const bool callbacks_remaining = self->_callback_inflight.sub (1);
    if (!callbacks_remaining
        && self->_handler_state.load (std::memory_order_acquire)
             != handler_active) {
        scoped_lock_t lock (self->_sync);
        self->_callback_cv.broadcast ();
    }
}

void spot_sub_t::handle_ready_probe (const std::string &raw_filter_,
                                     const std::string &peer_endpoint_)
{
    if (raw_filter_.empty ())
        return;
    if (_destroying.load (std::memory_order_acquire))
        return;
    if (_node && _node->is_shutting_down ())
        return;

    if (getenv ("ZLINK_DEBUG_SPOT_READY_PROBE")) {
        fprintf (stderr, "[spot-ready-probe] recv raw=%s endpoint=%s\n",
                 raw_filter_.c_str (),
                 peer_endpoint_.empty () ? "-" : peer_endpoint_.c_str ());
        fflush (stderr);
        FILE *fp = fopen ("/tmp/zlink_spot_ready_ack.log", "a");
        if (fp) {
            fprintf (fp, "probe raw=%s endpoint=%s\n", raw_filter_.c_str (),
                     peer_endpoint_.empty () ? "-" : peer_endpoint_.c_str ());
            fclose (fp);
        }
    }

    std::vector<subject_descriptor_t> subjects;
    append_subjects_for_raw_filter (raw_filter_, &subjects);
    if (subjects.empty ())
        return;
    const std::string endpoint =
      !peer_endpoint_.empty () ? peer_endpoint_ : first_ready_peer_endpoint ();
    const char *endpoint_ptr = endpoint.empty () ? NULL : endpoint.c_str ();
    {
        scoped_lock_t lock (_sync);
        _delivery_ready_raw_filters.insert (raw_filter_);
    }
    for (size_t i = 0; i < subjects.size (); ++i)
        mark_subject_ready (subjects[i], endpoint_ptr);

    if (endpoint.empty () || !_node)
        return;

    bool already_acked = false;
    {
        scoped_lock_t lock (_sync);
        std::map<std::string, std::set<std::string> >::const_iterator it =
          _ready_ack_endpoints.find (raw_filter_);
        already_acked =
          it != _ready_ack_endpoints.end ()
          && it->second.count (endpoint) != 0;
    }
    if (already_acked)
        return;

    if (_node->send_ready_ack_update (endpoint, raw_filter_,
                                      ready_ack_source_id (), true)
        != 0)
        return;

    {
        scoped_lock_t lock (_sync);
        _ready_ack_endpoints[raw_filter_].insert (endpoint);
    }
}

void spot_sub_t::release_ready_ack_endpoints (
  const std::string &raw_filter_,
  std::vector<std::string> *out_)
{
    if (!out_ || raw_filter_.empty ())
        return;

    out_->clear ();
    scoped_lock_t lock (_sync);
    std::map<std::string, std::set<std::string> >::iterator it =
      _ready_ack_endpoints.find (raw_filter_);
    if (it == _ready_ack_endpoints.end ())
        return;

    out_->insert (out_->end (), it->second.begin (), it->second.end ());
    _ready_ack_endpoints.erase (it);
}

void spot_sub_t::release_all_ready_ack_endpoints (
  std::vector<std::pair<std::string, std::string> > *out_)
{
    if (!out_)
        return;

    out_->clear ();
    scoped_lock_t lock (_sync);
    for (std::map<std::string, std::set<std::string> >::const_iterator it =
           _ready_ack_endpoints.begin ();
         it != _ready_ack_endpoints.end (); ++it) {
        for (std::set<std::string>::const_iterator endpoint_it =
               it->second.begin ();
             endpoint_it != it->second.end (); ++endpoint_it) {
            out_->push_back (std::make_pair (it->first, *endpoint_it));
        }
    }
    _ready_ack_endpoints.clear ();
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
    if (_destroying.load (std::memory_order_acquire)) {
        errno = EFSM;
        return -1;
    }
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
              _node->_lifecycle.close_socket (monitor_socket, 2000),
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
        zlink_service_event_t batch[2];
        switch (raw.event) {
            case ZLINK_EVENT_CONNECTED:
            case ZLINK_EVENT_ACCEPTED:
                fill_socket_monitor_event (&event, ZLINK_MONITOR_EVENT_PEER_UP,
                                           raw);
                emit_monitor_event (event);
                break;

            case ZLINK_EVENT_CONNECTION_READY: {
                {
                    scoped_lock_t lock (_sync);
                    if (raw.remote_addr[0] != '\0')
                        _ready_peer_endpoints.insert (raw.remote_addr);
                }
                fill_socket_monitor_event (&batch[0], ZLINK_MONITOR_EVENT_READY,
                                           raw);
                fill_socket_monitor_event (&batch[1], ZLINK_MONITOR_EVENT_PEER_UP,
                                           raw);
                emit_monitor_event_batch (&_monitor, batch, 2);
                break;
            }

            case ZLINK_EVENT_DISCONNECTED:
                {
                    scoped_lock_t lock (_sync);
                    if (raw.remote_addr[0] != '\0')
                        _ready_peer_endpoints.erase (raw.remote_addr);
                }
                fill_socket_monitor_event (&batch[0], ZLINK_MONITOR_EVENT_LOST,
                                           raw);
                fill_socket_monitor_event (&batch[1], ZLINK_MONITOR_EVENT_PEER_DOWN,
                                           raw);
                emit_monitor_event_batch (&_monitor, batch, 2);
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
                emit_monitor_event (event);
                break;

            default:
                break;
        }
    }
}

int spot_sub_t::destroy_internal (bool allow_embedded_default_,
                                  bool notify_node_)
{
    spot_sub_diag_log ("destroy.begin");
    if (_node_owned_default && !allow_embedded_default_) {
        errno = EINVAL;
        return -1;
    }

    _destroying.store (true, std::memory_order_release);

    socket_base_t *socket = this->socket ();
    int first_error = 0;
    std::vector<std::pair<std::string, std::string> > ready_ack_updates;
    bool had_filters = false;
    const bool node_shutting_down = _node && _node->is_shutting_down ();

    {
        scoped_lock_t lock (_sync);
        had_filters = !_topics.empty () || !_patterns.empty ();
    }

    bool has_handler = false;
    {
        scoped_lock_t lock (_sync);
        has_handler = _handler_state.load (std::memory_order_acquire)
                      != handler_none;
        if (has_handler)
            _handler_state.store (handler_clearing, std::memory_order_release);
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
        spot_sub_diag_log ("destroy.before-sub-dispatch-stop");
    if (has_handler && socket && socket->sub_dispatch_active ())
        socket->sub_dispatch_stop ();
    if (has_handler)
        spot_sub_diag_log ("destroy.after-sub-dispatch-stop");
    {
        scoped_lock_t lock (_sync);
        if (_callback_inflight.get () > 0) {
            errno = EBUSY;
            return -1;
        }
        _active_direct_handler.store (NULL, std::memory_order_release);
        _handler_state.store (handler_none, std::memory_order_release);
        _callback_cv.broadcast ();
    }
    spot_sub_diag_log ("destroy.before-stop-monitor-bridge");
    preserve_first_error (stop_monitor_bridge (), &first_error);
    spot_sub_diag_log ("destroy.after-stop-monitor-bridge");

    release_all_ready_ack_endpoints (&ready_ack_updates);
    if (_node && !node_shutting_down) {
        const std::string ack_source_id = ready_ack_source_id ();
        for (size_t i = 0; i < ready_ack_updates.size (); ++i) {
            (void) _node->send_ready_ack_update (ready_ack_updates[i].second,
                                                 ready_ack_updates[i].first,
                                                 ack_source_id, false);
        }
    }

    if (notify_node_ && _node)
        _node->remove_spot_sub (this);
    if (notify_node_ && _node)
        _node->submit_sub_summary (this, ZLINK_TOPOLOGY_STATE_STOPPED, 0);
    if (notify_node_ && _node && had_filters && !node_shutting_down) {
        _node->schedule_subscription_replay ();
        preserve_first_error (_node->replay_subscriptions_if_active_peers (),
                              &first_error);
    }
    {
        scoped_lock_t lock (_sync);
        _topics.clear ();
        _patterns.clear ();
        _delivery_ready_raw_filters.clear ();
        _ready_peer_endpoints.clear ();
        _ready_subject_endpoints.clear ();
        _ready_ack_endpoints.clear ();
    }

    zlink_service_event_t terminal;
    fill_terminal_monitor_event (&terminal, ZLINK_MONITOR_EVENT_CLOSED,
                                 _routing_id);
    _monitor.close_all (&terminal);
    spot_sub_diag_log ("destroy.after-monitor-close-all");

    if (socket) {
        if (_node && _node->_runtime)
            spot_sub_diag_log ("destroy.before-destroy-attachment");
        if (socket && _node && _node->_runtime) {
            preserve_first_error (
              _node->_runtime->destroy_attachment (_attachment_id),
              &first_error);
        }
        if (socket && _node && _node->_runtime)
            spot_sub_diag_log ("destroy.after-destroy-attachment");
        else {
            socket->stop ();
            socket->close ();
        }
    }
    _socket = NULL;
    _attachment_id = 0;
    _node = NULL;
    _node_owned_default = false;
    if (first_error != 0) {
        errno = first_error;
        return -1;
    }
    spot_sub_diag_log ("destroy.end");
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
