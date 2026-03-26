/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_sub.hpp"

#include "services/spot/spot_control_protocol.hpp"
#include "services/spot/spot_node.hpp"
#include "sockets/socket_base.hpp"

#include <stdio.h>
#include <string.h>

namespace zlink
{
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
}

std::string spot_sub_t::ready_ack_source_id () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    return routing_id_to_hex (_routing_id);
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
        if (socket->setsockopt (ZLINK_INTERNAL_OPT_SUBSCRIBE, topic.data (),
                                topic.size ())
            != 0)
            return -1;
        _topics.insert (topic);
        _delivery_ready_raw_filters.erase (topic);
        has_filters = !_topics.empty () || !_patterns.empty ();
    }
    {
        scoped_lock_t node_lock (_node->_sync);
        _node->_subject_last_changed_ms[make_subject_key (
          topic, ZLINK_SERVICE_EVENT_SUBJECT_TOPIC)] = zlink::clock_t ().now_ms ();
        _node->_summary_last_changed_ms = zlink::clock_t ().now_ms ();
    }

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
        if (socket->setsockopt (ZLINK_INTERNAL_OPT_SUBSCRIBE, prefix.data (),
                                prefix.size ())
            != 0)
            return -1;
        _patterns.insert (prefix);
        _delivery_ready_raw_filters.erase (prefix);
        has_filters = !_topics.empty () || !_patterns.empty ();
    }
    {
        scoped_lock_t node_lock (_node->_sync);
        _node->_subject_last_changed_ms[make_subject_key (
          prefix + "*", ZLINK_SERVICE_EVENT_SUBJECT_PATTERN)] =
          zlink::clock_t ().now_ms ();
        _node->_summary_last_changed_ms = zlink::clock_t ().now_ms ();
    }

    _node->note_local_sub_filters_changed (had_filters, has_filters);
    const std::string subject = prefix + "*";
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
    const bool is_topic =
      !is_pattern && is_valid_topic (topic_or_pattern_, &topic);
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
        if (socket->setsockopt (ZLINK_INTERNAL_OPT_UNSUBSCRIBE, filter.data (),
                                filter.size ())
            != 0)
            return -1;
        if (is_pattern)
            _patterns.erase (prefix);
        else
            _topics.erase (topic);
        _delivery_ready_raw_filters.erase (filter);
        has_filters_after = !_topics.empty () || !_patterns.empty ();
    }
    {
        scoped_lock_t node_lock (_node->_sync);
        _node->_subject_last_changed_ms[make_subject_key (
          subject.subject, subject.subject_kind)] = zlink::clock_t ().now_ms ();
        _node->_summary_last_changed_ms = zlink::clock_t ().now_ms ();
    }

    _node->note_local_sub_filters_changed (had_filters, has_filters_after);
    if (_node->send_subscription_update (filter, false) != 0)
        first_error = errno != 0 ? errno : EIO;
    release_ready_ack_endpoints (filter, &ready_ack_endpoints);
    const std::string ack_source_id = ready_ack_source_id ();
    for (size_t i = 0; i < ready_ack_endpoints.size (); ++i) {
        (void) _node->send_ready_ack_update (ready_ack_endpoints[i], filter,
                                             ack_source_id, false);
    }
    _node->submit_sub_summary (this, has_filters_after
                                       ? ZLINK_TOPOLOGY_STATE_READY
                                       : ZLINK_TOPOLOGY_STATE_CONNECTING,
                               0);
    mark_subject_lost (subject, NULL);
    if (first_error != 0) {
        errno = first_error;
        return -1;
    }
    return 0;
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

bool spot_sub_t::has_filters () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    return !_topics.empty () || !_patterns.empty ();
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
                                                uint32_t subject_kind_,
                                                uint32_t ready_count_)
{
    zlink_service_event_t event;
    {
        scoped_lock_t lock (_sync);
        fill_subject_monitor_event (&event,
                                    ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED,
                                    _routing_id, endpoint_, subject_,
                                    subject_kind_, ready_count_);
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
                                    ZLINK_SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED,
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

    if (!endpoint_ || endpoint_[0] == '\0' || !_node) {
        mark_subject_ready (subject_, endpoint_);
        emit_subscription_ready_event (endpoint_, subject_.subject.c_str (),
                                       subject_.subject_kind, 1);
        return;
    }

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
    if (already_acked) {
        mark_subject_ready (subject_, endpoint_);
        emit_subscription_ready_event (endpoint_, subject_.subject.c_str (),
                                       subject_.subject_kind, 1);
        return;
    }

    if (_node->send_ready_ack_update (endpoint_, raw_filter,
                                      ready_ack_source_id (), true)
        != 0)
        return;

    {
        scoped_lock_t lock (_sync);
        _ready_ack_endpoints[raw_filter].insert (endpoint_);
    }

    mark_subject_ready (subject_, endpoint_);
    emit_subscription_ready_event (endpoint_, subject_.subject.c_str (),
                                   subject_.subject_kind, 1);
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

    if (_node) {
        scoped_lock_t node_lock (_node->_sync);
        _node->_subject_last_changed_ms[make_subject_key (
          subject_.subject, subject_.subject_kind)] = zlink::clock_t ().now_ms ();
        _node->_summary_last_changed_ms = zlink::clock_t ().now_ms ();
    }

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

    if (_node) {
        scoped_lock_t node_lock (_node->_sync);
        _node->_subject_last_changed_ms[make_subject_key (
          subject_.subject, subject_.subject_kind)] = zlink::clock_t ().now_ms ();
        _node->_summary_last_changed_ms = zlink::clock_t ().now_ms ();
    }

    emit_subscription_ready_event (endpoint_, subject_.subject.c_str (),
                                   subject_.subject_kind, 0);
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
} // namespace zlink
