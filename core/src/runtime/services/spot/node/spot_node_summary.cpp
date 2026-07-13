/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/node/spot_node.hpp"
#include "services/spot/runtime/spot_handle.hpp"
#include "services/spot/pubsub/spot_pub.hpp"
#include "services/spot/pubsub/spot_sub.hpp"

#include "api/spot/request_reply/service_spot_request_reply_internal.hpp"
#include "core/c_api_copy_internal.hpp"
#include "utils/clock.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace zlink
{
namespace
{
struct subject_snapshot_key_t
{
    uint32_t subject_kind;
    std::string subject;

    bool operator== (const subject_snapshot_key_t &other_) const
    {
        return subject_kind == other_.subject_kind && subject == other_.subject;
    }
};

struct subject_snapshot_key_hash_t
{
    size_t operator() (const subject_snapshot_key_t &key_) const
    {
        size_t seed = std::hash<uint32_t> () (key_.subject_kind);
        seed ^= std::hash<std::string> () (key_.subject) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

static std::string spot_subject_snapshot_key_local (const std::string &subject_,
                                                    uint32_t subject_kind_)
{
    char prefix[16];
    snprintf (prefix, sizeof (prefix), "%u:", subject_kind_);
    return std::string (prefix) + subject_;
}

static uint32_t ready_subject_count_local (
  const std::vector<spot_node_summary_state_t::subject_snapshot_entry_t> &entries_)
{
    uint32_t ready_count = 0;
    for (size_t i = 0; i < entries_.size (); ++i) {
        if (entries_[i].ready)
            ++ready_count;
    }
    return ready_count;
}

}

std::string spot_node_t::summary_channel_name () const
{
    scoped_lock_t lock (_sync);
    if (service_attachments ().attachments.size () == 1)
        return service_attachments ().attachments.begin ()->first;
    return std::string ();
}

void spot_node_t::mark_subject_changed (const std::string &subject_, uint32_t subject_kind_)
{
    char prefix[16];
    snprintf (prefix, sizeof (prefix), "%u:", subject_kind_);
    const uint64_t now_ms = zlink::clock_t ().now_ms ();

    scoped_lock_t lock (_sync);
    _summary_state.subject_last_changed_ms[std::string (prefix) + subject_] = now_ms;
    _summary_state.summary_last_changed_ms = now_ms;
    _summary_state.mark_subject_snapshot_changed ();
}

void spot_node_t::submit_pub_summary (spot_pub_t *pub_, uint16_t state_, int error_code_)
{
    LIBZLINK_UNUSED (pub_);
    LIBZLINK_UNUSED (state_);
    LIBZLINK_UNUSED (error_code_);
}

void spot_node_t::submit_sub_summary (spot_sub_t *sub_, uint16_t state_, int error_code_)
{
    LIBZLINK_UNUSED (sub_);
    LIBZLINK_UNUSED (state_);
    LIBZLINK_UNUSED (error_code_);
}

void spot_node_t::submit_spot_owner_summary_for_rid (const zlink_routing_id_t &rid_,
                                                     zlink_spot_kind_t spot_kind_,
                                                     uint16_t state_,
                                                     int error_code_)
{
    LIBZLINK_UNUSED (rid_);
    LIBZLINK_UNUSED (spot_kind_);
    LIBZLINK_UNUSED (state_);
    LIBZLINK_UNUSED (error_code_);
}

void spot_node_t::submit_spot_owner_summary (const std::shared_ptr<spot_logical_state_t> &state_,
                                             uint16_t state,
                                             int error_code_)
{
    if (!state_)
        return;
    submit_spot_owner_summary_for_rid (state_->routing_id,
                                       state_->entry ? ZLINK_SPOT_KIND_ENTRY : ZLINK_SPOT_KIND_USER,
                                       state, error_code_);
}

void spot_node_t::submit_stopped_summaries ()
{
    std::vector<spot_pub_t *> pubs;
    std::vector<spot_sub_t *> subs;
    std::vector<std::shared_ptr<spot_logical_state_t>> spots;
    {
        scoped_lock_t lock (_sync);
        pubs.reserve (_handle_state.pubs.size ());
        subs.reserve (_handle_state.subs.size ());
        pubs.assign (_handle_state.pubs.begin (), _handle_state.pubs.end ());
        subs.assign (_handle_state.subs.begin (), _handle_state.subs.end ());
        spots.reserve (_handle_state.spots_by_rid.size ());
        for (std::map<std::string, std::shared_ptr<spot_logical_state_t>>::const_iterator it =
               _handle_state.spots_by_rid.begin ();
             it != _handle_state.spots_by_rid.end (); ++it)
            spots.push_back (it->second);
    }

    for (size_t i = 0; i < pubs.size (); ++i)
        submit_pub_summary (pubs[i], ZLINK_TOPOLOGY_STATE_STOPPED, 0);
    for (size_t i = 0; i < subs.size (); ++i)
        submit_sub_summary (subs[i], ZLINK_TOPOLOGY_STATE_STOPPED, 0);
    for (size_t i = 0; i < spots.size (); ++i)
        submit_spot_owner_summary (spots[i], ZLINK_TOPOLOGY_STATE_STOPPED, 0);
}

void spot_node_t::refresh_existing_summaries ()
{
    std::vector<spot_pub_t *> pubs;
    std::vector<spot_sub_t *> subs;
    std::vector<std::shared_ptr<spot_logical_state_t>> spots;
    bool bound = false;
    {
        scoped_lock_t lock (_sync);
        pubs.reserve (_handle_state.pubs.size ());
        subs.reserve (_handle_state.subs.size ());
        pubs.assign (_handle_state.pubs.begin (), _handle_state.pubs.end ());
        subs.assign (_handle_state.subs.begin (), _handle_state.subs.end ());
        spots.reserve (_handle_state.spots_by_rid.size ());
        for (std::map<std::string, std::shared_ptr<spot_logical_state_t>>::const_iterator it =
               _handle_state.spots_by_rid.begin ();
             it != _handle_state.spots_by_rid.end (); ++it)
            spots.push_back (it->second);
        bound = !_endpoint_state.bound_endpoint.empty ()
                || !_endpoint_state.router_bind_endpoint.empty ();
    }

    if (bound) {
        for (size_t i = 0; i < pubs.size (); ++i)
            submit_pub_summary (pubs[i], ZLINK_TOPOLOGY_STATE_READY, 0);
        for (size_t i = 0; i < spots.size (); ++i)
            submit_spot_owner_summary (spots[i], ZLINK_TOPOLOGY_STATE_READY, 0);
    }
    for (size_t i = 0; i < subs.size (); ++i) {
        const uint16_t state =
          subs[i]->has_filters () ? ZLINK_TOPOLOGY_STATE_READY : ZLINK_TOPOLOGY_STATE_CONNECTING;
        submit_sub_summary (subs[i], state, 0);
    }
}

void spot_node_t::refresh_sub_peer_summaries (bool has_active_peers, bool lost_transition)
{
    std::vector<spot_sub_t *> subs;
    {
        scoped_lock_t lock (_sync);
        subs.reserve (_handle_state.subs.size ());
        subs.assign (_handle_state.subs.begin (), _handle_state.subs.end ());
    }

    for (size_t i = 0; i < subs.size (); ++i) {
        if (lost_transition) {
            submit_sub_summary (subs[i], ZLINK_TOPOLOGY_STATE_LOST, 0);
        } else if (has_active_peers) {
            const bool ready = subs[i]->has_filters ();
            submit_sub_summary (
              subs[i], ready ? ZLINK_TOPOLOGY_STATE_READY : ZLINK_TOPOLOGY_STATE_CONNECTING, 0);
        }
    }
}

void spot_node_t::snapshot_raw_subscription_filters (std::set<std::string> *out_) const
{
    if (!out_)
        return;

    scoped_lock_t lock (_sync);
    for (std::unordered_map<std::string, uint32_t>::const_iterator it =
           _aggregate_subscriptions.local_exact_topic_refcount.begin ();
         it != _aggregate_subscriptions.local_exact_topic_refcount.end (); ++it) {
        if (it->second != 0)
            out_->insert (it->first);
    }
    for (std::unordered_map<std::string, uint32_t>::const_iterator it =
           _aggregate_subscriptions.local_prefix_topic_refcount.begin ();
         it != _aggregate_subscriptions.local_prefix_topic_refcount.end (); ++it) {
        if (it->second != 0)
            out_->insert (it->first);
    }
}

bool spot_node_t::update_aggregate_subscription (const std::string &raw_filter_,
                                                 bool pattern_,
                                                 bool subscribe_)
{
    if (raw_filter_.empty ())
        return false;

    scoped_lock_t lock (_sync);
    std::unordered_map<std::string, uint32_t> &refcounts =
      pattern_ ? _aggregate_subscriptions.local_prefix_topic_refcount
               : _aggregate_subscriptions.local_exact_topic_refcount;

    if (subscribe_) {
        uint32_t &count = refcounts[raw_filter_];
        const bool first = count == 0;
        ++count;
        return first;
    }

    std::unordered_map<std::string, uint32_t>::iterator it = refcounts.find (raw_filter_);
    if (it == refcounts.end () || it->second == 0)
        return false;
    --it->second;
    if (it->second != 0)
        return false;
    refcounts.erase (it);
    return true;
}

int spot_node_t::update_logical_spot_subscription (const std::string &raw_filter_,
                                                   bool pattern_,
                                                   bool subscribe_)
{
    if (raw_filter_.empty ()) {
        errno = EINVAL;
        return -1;
    }

    const bool changed = update_aggregate_subscription (raw_filter_, pattern_, subscribe_);
    if (!changed)
        return 0;

    //  Keep the reason the receiver could not be created. It is usually a
    //  transient one (the attachment handshake timing out under load), and
    //  overwriting it with ENOTSUP reports a permanent "pub/sub unsupported"
    //  for what a retry would have cleared.
    errno = 0;
    spot_internal_receiver_t *receiver = ensure_internal_receiver ();
    spot_sub_t *sub = receiver ? receiver->impl () : NULL;
    if (!sub) {
        const int err = errno != 0 ? errno : ENOTSUP;
        const bool rollback_changed =
          update_aggregate_subscription (raw_filter_, pattern_, !subscribe_);
        LIBZLINK_UNUSED (rollback_changed);
        errno = err;
        return -1;
    }

    if (sub->apply_aggregate_subscription (raw_filter_, pattern_, subscribe_) != 0) {
        const int err = errno;
        const bool rollback_changed =
          update_aggregate_subscription (raw_filter_, pattern_, !subscribe_);
        LIBZLINK_UNUSED (rollback_changed);
        errno = err;
        return -1;
    }

    if (send_subscription_update (raw_filter_, subscribe_) != 0)
        return -1;
    if (subscribe_) {
        if (has_active_peers ())
            notify_subscription_forwarded (raw_filter_);
        schedule_subscription_replay ();
        if (replay_subscriptions_if_active_peers () != 0)
            return -1;
    }
    return 0;
}

void spot_node_t::snapshot_subscription_subjects (
  std::vector<spot_sub_t::subject_descriptor_t> *out_) const
{
    if (!out_)
        return;

    scoped_lock_t lock (_sync);
    for (std::unordered_map<std::string, uint32_t>::const_iterator it =
           _aggregate_subscriptions.local_exact_topic_refcount.begin ();
         it != _aggregate_subscriptions.local_exact_topic_refcount.end (); ++it) {
        if (it->second == 0)
            continue;
        spot_sub_t::subject_descriptor_t subject;
        subject.subject = it->first;
        subject.subject_kind = ZLINK_SERVICE_EVENT_SUBJECT_TOPIC;
        out_->push_back (subject);
    }
    for (std::unordered_map<std::string, uint32_t>::const_iterator it =
           _aggregate_subscriptions.local_prefix_topic_refcount.begin ();
         it != _aggregate_subscriptions.local_prefix_topic_refcount.end (); ++it) {
        if (it->second == 0)
            continue;
        spot_sub_t::subject_descriptor_t subject;
        subject.subject = it->first + "*";
        subject.subject_kind = ZLINK_SERVICE_EVENT_SUBJECT_PATTERN;
        out_->push_back (subject);
    }
}

void spot_node_t::snapshot_subject_summary_entries (
  std::vector<spot_node_summary_state_t::subject_snapshot_entry_t> *out_) const
{
    if (!out_)
        return;
    out_->clear ();

    while (true) {
        std::vector<spot_sub_t *> subs;
        std::map<std::string, uint64_t> subject_last_changed;
        uint64_t generation = 0;
        {
            scoped_lock_t lock (_sync);
            if (_summary_state.cached_subject_snapshot_generation
                == _summary_state.subject_snapshot_generation) {
                *out_ = _summary_state.cached_subject_entries;
                return;
            }
            generation = _summary_state.subject_snapshot_generation;
            subs.reserve (_handle_state.subs.size ());
            subs.assign (_handle_state.subs.begin (), _handle_state.subs.end ());
            subject_last_changed = _summary_state.subject_last_changed_ms;
        }

        std::unordered_map<subject_snapshot_key_t,
                           spot_node_summary_state_t::subject_snapshot_entry_t,
                           subject_snapshot_key_hash_t>
          grouped;
        grouped.reserve (subs.size () * 2);
        for (size_t i = 0; i < subs.size (); ++i) {
            if (!subs[i])
                continue;
            std::vector<spot_sub_t::subject_snapshot_t> subjects;
            subs[i]->append_subject_snapshots (&subjects);
            for (size_t j = 0; j < subjects.size (); ++j) {
                subject_snapshot_key_t key;
                key.subject_kind = subjects[j].subject_kind;
                key.subject = subjects[j].subject;
                spot_node_summary_state_t::subject_snapshot_entry_t &entry = grouped[key];
                if (entry.subject_kind == 0 && entry.subject.empty ()) {
                    entry.subject = subjects[j].subject;
                    entry.subject_kind = subjects[j].subject_kind;
                    entry.ready = false;
                    entry.last_changed_ms = 0;
                }
                entry.ready = entry.ready || subjects[j].ready;
                std::map<std::string, uint64_t>::const_iterator tsit = subject_last_changed.find (
                  spot_subject_snapshot_key_local (subjects[j].subject, subjects[j].subject_kind));
                if (tsit != subject_last_changed.end () && tsit->second > entry.last_changed_ms) {
                    entry.last_changed_ms = tsit->second;
                }
            }
        }

        std::vector<spot_node_summary_state_t::subject_snapshot_entry_t> entries;
        entries.reserve (grouped.size ());
        for (std::unordered_map<subject_snapshot_key_t,
                                spot_node_summary_state_t::subject_snapshot_entry_t,
                                subject_snapshot_key_hash_t>::const_iterator it = grouped.begin ();
             it != grouped.end (); ++it) {
            entries.push_back (it->second);
        }

        scoped_lock_t lock (_sync);
        if (_summary_state.subject_snapshot_generation != generation)
            continue;
        _summary_state.cached_subject_entries = entries;
        _summary_state.cached_subject_snapshot_generation = generation;
        *out_ = _summary_state.cached_subject_entries;
        return;
    }
}

void spot_node_t::snapshot_status_subject_counts (uint32_t *subject_count_out_,
                                                  uint32_t *ready_subject_count_out_) const
{
    if (subject_count_out_)
        *subject_count_out_ = 0;
    if (ready_subject_count_out_)
        *ready_subject_count_out_ = 0;
    if (!subject_count_out_ && !ready_subject_count_out_)
        return;

    std::vector<spot_node_summary_state_t::subject_snapshot_entry_t> entries;
    snapshot_subject_summary_entries (&entries);
    if (subject_count_out_)
        *subject_count_out_ = static_cast<uint32_t> (entries.size ());
    if (ready_subject_count_out_)
        *ready_subject_count_out_ = ready_subject_count_local (entries);
}

}
