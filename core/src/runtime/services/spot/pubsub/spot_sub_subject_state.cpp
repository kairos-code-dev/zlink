/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/pubsub/spot_sub.hpp"

#include "services/spot/common/spot_control_protocol.hpp"
#include "services/spot/node/spot_node.hpp"
#include "sockets/common/socket_base.hpp"

namespace zlink
{
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
    bool inserted = false;
    {
        scoped_lock_t lock (_sync);
        had_filters = !_topics.empty () || !_patterns.empty ();
        lock_routing_id ();
        inserted = _topics.count (topic) == 0;
        if (inserted) {
            if (socket->setsockopt (ZLINK_INTERNAL_OPT_SUBSCRIBE, topic.data (), topic.size ())
                != 0)
                return -1;
            _topics.insert (topic);
            _delivery_ready_raw_filters.erase (topic);
        }
        has_filters = !_topics.empty () || !_patterns.empty ();
    }
    _node->mark_subject_changed (topic, ZLINK_SERVICE_EVENT_SUBJECT_TOPIC);

    _node->note_local_sub_filters_changed (had_filters, has_filters);
    emit_filter_applied_event (topic.c_str (), ZLINK_SERVICE_EVENT_SUBJECT_TOPIC);
    if (inserted) {
        const bool aggregate_added = _node->update_aggregate_subscription (topic, false, true);
        if (aggregate_added && _node->send_subscription_update (topic, true) != 0)
            return -1;
        if (_node->has_active_peers ())
            _node->notify_subscription_forwarded (topic);
        _node->schedule_subscription_replay ();
        if (aggregate_added && _node->replay_subscriptions_if_active_peers () != 0)
            return -1;
    }
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
    bool inserted = false;
    {
        scoped_lock_t lock (_sync);
        had_filters = !_topics.empty () || !_patterns.empty ();
        lock_routing_id ();
        inserted = _patterns.count (prefix) == 0;
        if (inserted) {
            if (socket->setsockopt (ZLINK_INTERNAL_OPT_SUBSCRIBE, prefix.data (), prefix.size ())
                != 0)
                return -1;
            _patterns.insert (prefix);
            _delivery_ready_raw_filters.erase (prefix);
        }
        has_filters = !_topics.empty () || !_patterns.empty ();
    }
    _node->mark_subject_changed (prefix + "*", ZLINK_SERVICE_EVENT_SUBJECT_PATTERN);

    _node->note_local_sub_filters_changed (had_filters, has_filters);
    const std::string subject = prefix + "*";
    emit_filter_applied_event (subject.c_str (), ZLINK_SERVICE_EVENT_SUBJECT_PATTERN);
    if (inserted) {
        const bool aggregate_added = _node->update_aggregate_subscription (prefix, true, true);
        if (aggregate_added && _node->send_subscription_update (prefix, true) != 0)
            return -1;
        if (_node->has_active_peers ())
            _node->notify_subscription_forwarded (prefix);
        _node->schedule_subscription_replay ();
        if (aggregate_added && _node->replay_subscriptions_if_active_peers () != 0)
            return -1;
    }
    _node->submit_sub_summary (this, ZLINK_TOPOLOGY_STATE_READY, 0);
    return 0;
}

int spot_sub_t::apply_aggregate_subscription (const std::string &raw_filter_,
                                              bool pattern_,
                                              bool subscribe_)
{
    socket_base_t *socket = this->socket ();
    if (!_node || !socket || raw_filter_.empty ()) {
        errno = EFAULT;
        return -1;
    }

    const uint32_t subject_kind =
      pattern_ ? ZLINK_SERVICE_EVENT_SUBJECT_PATTERN : ZLINK_SERVICE_EVENT_SUBJECT_TOPIC;
    const std::string subject = pattern_ ? raw_filter_ + "*" : raw_filter_;
    bool had_filters = false;
    bool has_filters = false;
    bool changed = false;
    {
        scoped_lock_t lock (_sync);
        had_filters = !_topics.empty () || !_patterns.empty ();
        lock_routing_id ();
        std::set<std::string> &filters = pattern_ ? _patterns : _topics;
        if (subscribe_) {
            changed = filters.count (raw_filter_) == 0;
            if (changed) {
                if (socket->setsockopt (ZLINK_INTERNAL_OPT_SUBSCRIBE, raw_filter_.data (),
                                        raw_filter_.size ())
                    != 0)
                    return -1;
                filters.insert (raw_filter_);
                _delivery_ready_raw_filters.erase (raw_filter_);
            }
        } else {
            changed = filters.count (raw_filter_) != 0;
            if (changed) {
                if (socket->setsockopt (ZLINK_INTERNAL_OPT_UNSUBSCRIBE, raw_filter_.data (),
                                        raw_filter_.size ())
                    != 0)
                    return -1;
                filters.erase (raw_filter_);
                _delivery_ready_raw_filters.erase (raw_filter_);
            }
        }
        has_filters = !_topics.empty () || !_patterns.empty ();
    }

    if (!changed)
        return 0;

    _node->mark_subject_changed (subject, subject_kind);

    _node->note_local_sub_filters_changed (had_filters, has_filters);
    if (subscribe_) {
        emit_filter_applied_event (subject.c_str (), subject_kind);
        _node->submit_sub_summary (this, ZLINK_TOPOLOGY_STATE_READY, 0);
    } else {
        _node->submit_sub_summary (
          this, has_filters ? ZLINK_TOPOLOGY_STATE_READY : ZLINK_TOPOLOGY_STATE_CONNECTING, 0);
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
    bool erased = false;
    bool aggregate_removed = false;
    int first_error = 0;
    std::vector<std::string> ready_ack_endpoints;
    subject_descriptor_t subject;
    subject.subject_kind =
      is_pattern ? ZLINK_SERVICE_EVENT_SUBJECT_PATTERN : ZLINK_SERVICE_EVENT_SUBJECT_TOPIC;
    subject.subject = is_pattern ? prefix + "*" : topic;
    const std::string filter = is_pattern ? prefix : topic;
    {
        scoped_lock_t lock (_sync);
        had_filters = !_topics.empty () || !_patterns.empty ();
        lock_routing_id ();
        erased = is_pattern ? _patterns.count (prefix) != 0 : _topics.count (topic) != 0;
        if (erased) {
            if (socket->setsockopt (ZLINK_INTERNAL_OPT_UNSUBSCRIBE, filter.data (), filter.size ())
                != 0)
                return -1;
            if (is_pattern)
                _patterns.erase (prefix);
            else
                _topics.erase (topic);
            _delivery_ready_raw_filters.erase (filter);
        }
        has_filters_after = !_topics.empty () || !_patterns.empty ();
    }
    _node->mark_subject_changed (subject.subject, subject.subject_kind);

    _node->note_local_sub_filters_changed (had_filters, has_filters_after);
    if (erased) {
        aggregate_removed = _node->update_aggregate_subscription (filter, is_pattern, false);
        if (aggregate_removed && _node->send_subscription_update (filter, false) != 0)
            first_error = errno != 0 ? errno : EIO;
    }
    release_ready_ack_endpoints (filter, &ready_ack_endpoints);
    const std::string ack_source_id = ready_ack_source_id ();
    if (aggregate_removed) {
        for (size_t i = 0; i < ready_ack_endpoints.size (); ++i) {
            (void) _node->send_ready_ack_update (ready_ack_endpoints[i], filter, ack_source_id,
                                                 false);
        }
    }
    _node->submit_sub_summary (
      this, has_filters_after ? ZLINK_TOPOLOGY_STATE_READY : ZLINK_TOPOLOGY_STATE_CONNECTING, 0);
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

    scoped_lock_t lock (_sync);
    out_->insert (_topics.begin (), _topics.end ());
    out_->insert (_patterns.begin (), _patterns.end ());
}

void spot_sub_t::append_replay_raw_filters (std::set<std::string> *out_) const
{
    if (!out_)
        return;

    scoped_lock_t lock (_sync);
    for (std::set<std::string>::const_iterator it = _topics.begin (); it != _topics.end (); ++it) {
        if (_delivery_ready_raw_filters.count (*it) == 0)
            out_->insert (*it);
    }
    for (std::set<std::string>::const_iterator it = _patterns.begin (); it != _patterns.end ();
         ++it) {
        if (_delivery_ready_raw_filters.count (*it) == 0)
            out_->insert (*it);
    }
}

bool spot_sub_t::has_filters () const
{
    scoped_lock_t lock (_sync);
    return !_topics.empty () || !_patterns.empty ();
}

void spot_sub_t::append_all_subjects (std::vector<subject_descriptor_t> *out_) const
{
    if (!out_)
        return;

    scoped_lock_t lock (_sync);
    out_->reserve (out_->size () + _topics.size () + _patterns.size ());
    for (std::set<std::string>::const_iterator it = _topics.begin (); it != _topics.end (); ++it) {
        subject_descriptor_t subject;
        subject.subject = *it;
        subject.subject_kind = ZLINK_SERVICE_EVENT_SUBJECT_TOPIC;
        out_->push_back (subject);
    }
    for (std::set<std::string>::const_iterator it = _patterns.begin (); it != _patterns.end ();
         ++it) {
        subject_descriptor_t subject;
        subject.subject = *it + "*";
        subject.subject_kind = ZLINK_SERVICE_EVENT_SUBJECT_PATTERN;
        out_->push_back (subject);
    }
}

void spot_sub_t::append_subjects_for_raw_filter (const std::string &raw_filter_,
                                                 std::vector<subject_descriptor_t> *out_) const
{
    if (!out_ || raw_filter_.empty ())
        return;

    scoped_lock_t lock (_sync);
    out_->reserve (out_->size () + 2);
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

void spot_sub_t::emit_filter_applied_event (const char *subject_, uint32_t subject_kind_)
{
    LIBZLINK_UNUSED (subject_);
    LIBZLINK_UNUSED (subject_kind_);
}
} // namespace zlink
