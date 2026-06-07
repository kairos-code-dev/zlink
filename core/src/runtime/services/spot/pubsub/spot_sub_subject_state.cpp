/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/pubsub/spot_sub.hpp"

#include "services/spot/common/spot_control_protocol.hpp"
#include "services/spot/common/spot_debug.hpp"
#include "services/spot/node/spot_node.hpp"
#include "sockets/common/socket_base.hpp"

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

static std::string make_subject_key (const std::string &subject_, uint32_t subject_kind_)
{
    char prefix[16];
    snprintf (prefix, sizeof (prefix), "%u:", subject_kind_);
    return std::string (prefix) + subject_;
}
}

std::string spot_sub_t::ready_ack_source_id () const
{
    if (_node) {
        zlink_routing_id_t node_rid;
        memset (&node_rid, 0, sizeof (node_rid));
        if (_node->node_routing_id (&node_rid) == 0 && node_rid.size > 0)
            return routing_id_to_hex (node_rid);
    }
    scoped_lock_t lock (_sync);
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

void spot_sub_t::append_subject_snapshots (std::vector<subject_snapshot_t> *out_) const
{
    if (!out_)
        return;

    scoped_lock_t lock (_sync);
    out_->reserve (out_->size () + _topics.size () + _patterns.size ());
    for (std::set<std::string>::const_iterator it = _topics.begin (); it != _topics.end (); ++it) {
        subject_snapshot_t subject;
        subject.subject = *it;
        subject.subject_kind = ZLINK_SERVICE_EVENT_SUBJECT_TOPIC;
        subject.ready =
          _ready_subject_endpoints.count (make_subject_key (subject.subject, subject.subject_kind))
          != 0;
        out_->push_back (subject);
    }
    for (std::set<std::string>::const_iterator it = _patterns.begin (); it != _patterns.end ();
         ++it) {
        subject_snapshot_t subject;
        subject.subject = *it + "*";
        subject.subject_kind = ZLINK_SERVICE_EVENT_SUBJECT_PATTERN;
        subject.ready =
          _ready_subject_endpoints.count (make_subject_key (subject.subject, subject.subject_kind))
          != 0;
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

void spot_sub_t::mark_subject_subscription_ready (const subject_descriptor_t &subject_,
                                                  const char *endpoint_)
{
    if (subject_.subject.empty () || subject_.subject_kind == ZLINK_SERVICE_EVENT_SUBJECT_NONE)
        return;

    if (!endpoint_ || endpoint_[0] == '\0' || !_node) {
        mark_subject_ready (subject_, endpoint_);
        return;
    }

    std::string raw_filter = subject_.subject;
    if (subject_.subject_kind == ZLINK_SERVICE_EVENT_SUBJECT_PATTERN && !raw_filter.empty ()
        && raw_filter[raw_filter.size () - 1] == '*') {
        raw_filter.erase (raw_filter.size () - 1);
    }
    if (raw_filter.empty ())
        return;

    bool already_acked = false;
    {
        scoped_lock_t lock (_sync);
        std::map<std::string, std::set<std::string>>::const_iterator it =
          _ready_ack_endpoints.find (raw_filter);
        already_acked = it != _ready_ack_endpoints.end () && it->second.count (endpoint_) != 0;
    }
    if (already_acked) {
        mark_subject_ready (subject_, endpoint_);
        return;
    }

    if (_node->send_ready_ack_update (endpoint_, raw_filter, ready_ack_source_id (), true) != 0)
        return;

    {
        scoped_lock_t lock (_sync);
        _ready_ack_endpoints[raw_filter].insert (endpoint_);
    }

    mark_subject_ready (subject_, endpoint_);
}

std::string spot_sub_t::first_ready_peer_endpoint () const
{
    scoped_lock_t lock (_sync);
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
        raw_filters.reserve (_ready_ack_endpoints.size ());
        for (std::map<std::string, std::set<std::string>>::const_iterator it =
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
        (void) _node->send_ready_ack_update (endpoint, raw_filters[i], ack_source_id, false);
}

void spot_sub_t::emit_ready_event ()
{
}

void spot_sub_t::mark_subject_ready (const subject_descriptor_t &subject_, const char *endpoint_)
{
    if (subject_.subject.empty () || subject_.subject_kind == ZLINK_SERVICE_EVENT_SUBJECT_NONE)
        return;

    bool emit = false;
    std::string endpoint_value;
    {
        scoped_lock_t lock (_sync);
        const std::string key = make_subject_key (subject_.subject, subject_.subject_kind);
        std::map<std::string, std::string>::iterator it = _ready_subject_endpoints.find (key);
        const std::string new_endpoint =
          endpoint_ && endpoint_[0] != '\0' ? std::string (endpoint_) : std::string ();
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

    if (_node)
        _node->mark_subject_changed (subject_.subject, subject_.subject_kind);

    LIBZLINK_UNUSED (endpoint_value);
}

void spot_sub_t::backfill_subject_ready_endpoint (const subject_descriptor_t &subject_,
                                                  const char *endpoint_)
{
    if (subject_.subject.empty () || subject_.subject_kind == ZLINK_SERVICE_EVENT_SUBJECT_NONE
        || !endpoint_ || endpoint_[0] == '\0')
        return;

    bool emit = false;
    {
        scoped_lock_t lock (_sync);
        const std::string key = make_subject_key (subject_.subject, subject_.subject_kind);
        std::map<std::string, std::string>::iterator it = _ready_subject_endpoints.find (key);
        if (spot_debug::enabled ("ZLINK_DEBUG_SPOT_BACKFILL")) {
            fprintf (stderr,
                     "[spot-backfill] subject=%s kind=%u endpoint=%s found=%d "
                     "stored=%s\n",
                     subject_.subject.c_str (), static_cast<unsigned int> (subject_.subject_kind),
                     endpoint_, it != _ready_subject_endpoints.end () ? 1 : 0,
                     (it != _ready_subject_endpoints.end () && !it->second.empty ())
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

    LIBZLINK_UNUSED (endpoint_);
}

void spot_sub_t::mark_subject_lost (const subject_descriptor_t &subject_, const char *endpoint_)
{
    if (subject_.subject.empty () || subject_.subject_kind == ZLINK_SERVICE_EVENT_SUBJECT_NONE)
        return;

    bool erased = false;
    {
        scoped_lock_t lock (_sync);
        erased = _ready_subject_endpoints.erase (
                   make_subject_key (subject_.subject, subject_.subject_kind))
                 != 0;
    }
    if (!erased)
        return;

    if (_node)
        _node->mark_subject_changed (subject_.subject, subject_.subject_kind);

    LIBZLINK_UNUSED (endpoint_);
}

void spot_sub_t::mark_all_subjects_lost (const char *endpoint_)
{
    std::vector<subject_descriptor_t> subjects;
    std::vector<std::pair<std::string, std::string>> ready_ack_updates;
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
                                                 ready_ack_updates[i].first, ack_source_id, false);
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

    if (spot_debug::enabled ("ZLINK_DEBUG_SPOT_READY_PROBE")) {
        fprintf (stderr, "[spot-ready-probe] recv raw=%s endpoint=%s\n", raw_filter_.c_str (),
                 peer_endpoint_.empty () ? "-" : peer_endpoint_.c_str ());
        fflush (stderr);
        FILE *fp = fopen (spot_debug::ready_ack_log_path (), "a");
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
        std::map<std::string, std::set<std::string>>::const_iterator it =
          _ready_ack_endpoints.find (raw_filter_);
        already_acked = it != _ready_ack_endpoints.end () && it->second.count (endpoint) != 0;
    }
    if (already_acked)
        return;

    if (_node->send_ready_ack_update (endpoint, raw_filter_, ready_ack_source_id (), true) != 0)
        return;

    {
        scoped_lock_t lock (_sync);
        _ready_ack_endpoints[raw_filter_].insert (endpoint);
    }
}

void spot_sub_t::release_ready_ack_endpoints (const std::string &raw_filter_,
                                              std::vector<std::string> *out_)
{
    if (!out_ || raw_filter_.empty ())
        return;

    out_->clear ();
    scoped_lock_t lock (_sync);
    std::map<std::string, std::set<std::string>>::iterator it =
      _ready_ack_endpoints.find (raw_filter_);
    if (it == _ready_ack_endpoints.end ())
        return;

    out_->reserve (it->second.size ());
    out_->insert (out_->end (), it->second.begin (), it->second.end ());
    _ready_ack_endpoints.erase (it);
}

void spot_sub_t::release_all_ready_ack_endpoints (
  std::vector<std::pair<std::string, std::string>> *out_)
{
    if (!out_)
        return;

    out_->clear ();
    scoped_lock_t lock (_sync);
    size_t ready_ack_count = 0;
    for (std::map<std::string, std::set<std::string>>::const_iterator it =
           _ready_ack_endpoints.begin ();
         it != _ready_ack_endpoints.end (); ++it)
        ready_ack_count += it->second.size ();
    out_->reserve (ready_ack_count);
    for (std::map<std::string, std::set<std::string>>::const_iterator it =
           _ready_ack_endpoints.begin ();
         it != _ready_ack_endpoints.end (); ++it) {
        for (std::set<std::string>::const_iterator endpoint_it = it->second.begin ();
             endpoint_it != it->second.end (); ++endpoint_it) {
            out_->push_back (std::make_pair (it->first, *endpoint_it));
        }
    }
    _ready_ack_endpoints.clear ();
}
} // namespace zlink
