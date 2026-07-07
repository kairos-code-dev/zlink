/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/pubsub/spot_sub.hpp"

#include "services/spot/common/spot_debug.hpp"
#include "services/spot/node/spot_node.hpp"

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
}
