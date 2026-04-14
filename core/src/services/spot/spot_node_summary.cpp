/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_node.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_runtime.hpp"
#include "services/spot/spot_sub.hpp"

#include "utils/clock.hpp"

#include <algorithm>
#include <cstring>

namespace zlink
{
namespace
{
static void copy_text_field_local (char *dst_,
                                   size_t dst_size_,
                                   const std::string &src_)
{
    if (!dst_ || dst_size_ == 0)
        return;
    dst_[0] = '\0';
    if (src_.empty ())
        return;
    strncpy (dst_, src_.c_str (), dst_size_ - 1);
    dst_[dst_size_ - 1] = '\0';
}

static bool spot_peer_filter_match_local (
  const zlink_spot_node_peer_entry_t &entry_,
  const zlink_spot_node_peer_filter_t *filter_)
{
    if (!filter_)
        return true;
    if (filter_->peer_endpoint[0] != '\0'
        && strcmp (filter_->peer_endpoint, entry_.peer_endpoint) != 0) {
        return false;
    }
    if (filter_->source != 0 && filter_->source != entry_.source)
        return false;
    if (filter_->state != 0 && filter_->state != entry_.state)
        return false;
    return true;
}

static bool spot_peer_entry_less_local (
  const zlink_spot_node_peer_entry_t &lhs_,
  const zlink_spot_node_peer_entry_t &rhs_)
{
    return strcmp (lhs_.peer_endpoint, rhs_.peer_endpoint) < 0;
}

static bool spot_subject_entry_less_local (
  const zlink_spot_node_subject_entry_t &lhs_,
  const zlink_spot_node_subject_entry_t &rhs_)
{
    if (lhs_.subject_kind != rhs_.subject_kind)
        return lhs_.subject_kind < rhs_.subject_kind;
    return strcmp (lhs_.subject, rhs_.subject) < 0;
}

static std::string spot_subject_snapshot_key_local (
  const std::string &subject_,
  uint32_t subject_kind_)
{
    char prefix[16];
    snprintf (prefix, sizeof (prefix), "%u:", subject_kind_);
    return std::string (prefix) + subject_;
}
}

std::string spot_node_t::summary_service_name () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    return _discovery_service;
}

void spot_node_t::submit_pub_summary (spot_pub_t *pub_,
                                      uint16_t state_,
                                      int error_code_)
{
    if (!pub_)
        return;

    discovery_t *discovery = NULL;
    std::string service_name;
    std::string endpoint;
    {
        scoped_lock_t lock (_sync);
        discovery = _discovery;
        service_name = _discovery_service;
        endpoint = _advertise_endpoint.empty () ? _bound_endpoint : _advertise_endpoint;
    }
    if (!discovery || service_name.empty ())
        return;

    zlink_routing_id_t rid;
    if (pub_->routing_id (&rid) != 0 || rid.size == 0)
        return;

    zlink_registry_topology_entry_t entry;
    memset (&entry, 0, sizeof (entry));
    entry.routing_id = rid;
    entry.service_kind = ZLINK_SERVICE_KIND_SPOT_PUB;
    entry.service_role = ZLINK_SERVICE_ROLE_SPOT;
    strncpy (entry.service_name, service_name.c_str (),
             sizeof (entry.service_name) - 1);
    strncpy (entry.endpoint, endpoint.c_str (), sizeof (entry.endpoint) - 1);
    entry.source = ZLINK_TOPOLOGY_SOURCE_DISCOVERY;
    entry.state = static_cast<zlink_topology_state_t> (state_);
    entry.desired_count = 1;
    entry.ready_count = state_ == ZLINK_TOPOLOGY_STATE_READY ? 1 : 0;
    entry.error_code = static_cast<uint32_t> (error_code_ > 0 ? error_code_ : 0);
    entry.last_reported_ms = clock_t ().now_ms ();
    discovery->upsert_service_summary (entry);
}

void spot_node_t::submit_sub_summary (spot_sub_t *sub_,
                                      uint16_t state_,
                                      int error_code_)
{
    if (!sub_)
        return;

    discovery_t *discovery = NULL;
    std::string service_name;
    {
        scoped_lock_t lock (_sync);
        discovery = _discovery;
        service_name = _discovery_service;
    }
    if (!discovery || service_name.empty ())
        return;

    zlink_routing_id_t rid;
    if (sub_->routing_id (&rid) != 0 || rid.size == 0)
        return;

    zlink_registry_topology_entry_t entry;
    memset (&entry, 0, sizeof (entry));
    entry.routing_id = rid;
    entry.service_kind = ZLINK_SERVICE_KIND_SPOT_SUB;
    entry.service_role = ZLINK_SERVICE_ROLE_SPOT;
    strncpy (entry.service_name, service_name.c_str (),
             sizeof (entry.service_name) - 1);
    entry.source = ZLINK_TOPOLOGY_SOURCE_DISCOVERY;
    entry.state = static_cast<zlink_topology_state_t> (state_);
    entry.desired_count = 1;
    entry.ready_count = state_ == ZLINK_TOPOLOGY_STATE_READY ? 1 : 0;
    entry.error_code = static_cast<uint32_t> (error_code_ > 0 ? error_code_ : 0);
    entry.last_reported_ms = clock_t ().now_ms ();
    discovery->upsert_service_summary (entry);
}

void spot_node_t::submit_stopped_summaries ()
{
    std::vector<spot_pub_t *> pubs;
    std::vector<spot_sub_t *> subs;
    {
        scoped_lock_t lock (_sync);
        pubs.assign (_pubs.begin (), _pubs.end ());
        subs.assign (_subs.begin (), _subs.end ());
    }

    for (size_t i = 0; i < pubs.size (); ++i)
        submit_pub_summary (pubs[i], ZLINK_TOPOLOGY_STATE_STOPPED, 0);
    for (size_t i = 0; i < subs.size (); ++i)
        submit_sub_summary (subs[i], ZLINK_TOPOLOGY_STATE_STOPPED, 0);
}

void spot_node_t::refresh_existing_summaries ()
{
    std::vector<spot_pub_t *> pubs;
    std::vector<spot_sub_t *> subs;
    bool bound = false;
    {
        scoped_lock_t lock (_sync);
        pubs.assign (_pubs.begin (), _pubs.end ());
        subs.assign (_subs.begin (), _subs.end ());
        bound = !_bound_endpoint.empty ();
    }

    if (bound) {
        for (size_t i = 0; i < pubs.size (); ++i)
            submit_pub_summary (pubs[i], ZLINK_TOPOLOGY_STATE_READY, 0);
    }
    for (size_t i = 0; i < subs.size (); ++i) {
        const uint16_t state =
          subs[i]->has_filters () ? ZLINK_TOPOLOGY_STATE_READY
                                  : ZLINK_TOPOLOGY_STATE_CONNECTING;
        submit_sub_summary (subs[i], state, 0);
    }
}

void spot_node_t::refresh_sub_peer_summaries (bool has_active_peers,
                                              bool lost_transition)
{
    std::vector<spot_sub_t *> subs;
    {
        scoped_lock_t lock (_sync);
        subs.assign (_subs.begin (), _subs.end ());
    }

    for (size_t i = 0; i < subs.size (); ++i) {
        if (lost_transition) {
            submit_sub_summary (subs[i], ZLINK_TOPOLOGY_STATE_LOST, 0);
        } else if (has_active_peers) {
            const bool ready = subs[i]->has_filters ();
            submit_sub_summary (subs[i], ready ? ZLINK_TOPOLOGY_STATE_READY
                                               : ZLINK_TOPOLOGY_STATE_CONNECTING,
                                0);
        }
    }
}

void spot_node_t::snapshot_raw_subscription_filters (
  std::set<std::string> *out_) const
{
    if (!out_)
        return;

    std::vector<spot_sub_t *> subs;
    {
        scoped_lock_t lock (const_cast<mutex_t &> (_sync));
        subs.assign (_subs.begin (), _subs.end ());
    }

    for (size_t i = 0; i < subs.size (); ++i)
        subs[i]->append_raw_filters (out_);
}

void spot_node_t::snapshot_subscription_subjects (
  std::vector<spot_sub_t::subject_descriptor_t> *out_) const
{
    if (!out_)
        return;

    std::vector<spot_sub_t *> subs;
    {
        scoped_lock_t lock (const_cast<mutex_t &> (_sync));
        subs.assign (_subs.begin (), _subs.end ());
    }

    for (size_t i = 0; i < subs.size (); ++i)
        subs[i]->append_all_subjects (out_);
}

int spot_node_t::snapshot_status (zlink_spot_node_status_t *out_) const
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }

    memset (out_, 0, sizeof (*out_));

    std::string service_name;
    std::string local_endpoint;
    {
        scoped_lock_t lock (const_cast<mutex_t &> (_sync));
        service_name = _discovery_service;
        local_endpoint =
          !_advertise_endpoint.empty () ? _advertise_endpoint : _bound_endpoint;
        out_->configured_peer_count =
          static_cast<uint32_t> (_peer_state.manual_endpoints.size ()
                                 + _peer_state.discovery_endpoints.size ());
        {
            std::set<std::string> union_peers = _peer_state.manual_endpoints;
            union_peers.insert (_peer_state.discovery_endpoints.begin (),
                                _peer_state.discovery_endpoints.end ());
            out_->configured_peer_count = static_cast<uint32_t> (union_peers.size ());
        }
        out_->active_peer_count =
          static_cast<uint32_t> (_peer_state.active_endpoints.size ());
        out_->connected_peer_count =
          static_cast<uint32_t> (_peer_state.connected_endpoints.size ());
        out_->last_error = _last_summary_error;
        if (_runtime && _runtime->faulted)
            out_->last_error = _runtime->fault_errno;
        out_->last_changed_ms = _summary_last_changed_ms;
    }

    copy_text_field_local (out_->service_name, sizeof (out_->service_name),
                           service_name);
    copy_text_field_local (out_->local_endpoint, sizeof (out_->local_endpoint),
                           local_endpoint);

    spot_pub_t *pub = default_pub ();
    spot_sub_t *sub = default_sub ();
    if (pub)
        (void) pub->routing_id (&out_->node_routing_id);
    else if (sub)
        (void) sub->routing_id (&out_->node_routing_id);

    std::vector<zlink_spot_node_subject_entry_t> subject_rows;
    if (snapshot_subjects (NULL, &subject_rows) == 0) {
        out_->subject_count = static_cast<uint32_t> (subject_rows.size ());
        for (size_t i = 0; i < subject_rows.size (); ++i) {
            if (subject_rows[i].ready_peer_count > 0)
                out_->ready_subject_count++;
        }
    }

    if (out_->last_error != 0)
        out_->state = ZLINK_SPOT_NODE_STATE_ERROR;
    else if (out_->configured_peer_count == 0 && out_->subject_count == 0)
        out_->state = ZLINK_SPOT_NODE_STATE_IDLE;
    else if (out_->active_peer_count > 0 && out_->connected_peer_count == 0)
        out_->state = ZLINK_SPOT_NODE_STATE_CONNECTING;
    else if (out_->connected_peer_count > 0
             && out_->ready_subject_count < out_->subject_count) {
        out_->state = ZLINK_SPOT_NODE_STATE_PARTIAL_READY;
    } else if (out_->connected_peer_count > 0
               && out_->subject_count > 0
               && out_->ready_subject_count == out_->subject_count) {
        out_->state = ZLINK_SPOT_NODE_STATE_READY;
    } else
        out_->state = ZLINK_SPOT_NODE_STATE_IDLE;

    return 0;
}

int spot_node_t::snapshot_peers (
  const zlink_spot_node_peer_filter_t *filter_,
  std::vector<zlink_spot_node_peer_entry_t> *out_) const
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }
    out_->clear ();

    std::string service_name;
    std::string local_endpoint;
    std::set<std::string> manual;
    std::set<std::string> discovery;
    std::set<std::string> active;
    std::set<std::string> connected;
    std::map<std::string, spot_peer_observation_t> observations;
    std::map<std::string, zlink_admission_state_t> admission_by_endpoint;
    {
        scoped_lock_t lock (const_cast<mutex_t &> (_sync));
        service_name = _discovery_service;
        local_endpoint =
          !_advertise_endpoint.empty () ? _advertise_endpoint : _bound_endpoint;
        manual = _peer_state.manual_endpoints;
        discovery = _peer_state.discovery_endpoints;
        active = _peer_state.active_endpoints;
        connected = _peer_state.connected_endpoints;
        observations = _peer_state.observations;
        admission_by_endpoint = _peer_state.peer_admission_by_endpoint;
    }

    std::set<std::string> universe = manual;
    universe.insert (discovery.begin (), discovery.end ());
    for (std::set<std::string>::const_iterator it = universe.begin ();
         it != universe.end (); ++it) {
        zlink_spot_node_peer_entry_t entry;
        memset (&entry, 0, sizeof (entry));
        copy_text_field_local (entry.service_name, sizeof (entry.service_name),
                               service_name);
        copy_text_field_local (entry.local_endpoint, sizeof (entry.local_endpoint),
                               local_endpoint);
        copy_text_field_local (entry.peer_endpoint, sizeof (entry.peer_endpoint),
                               *it);
        const bool in_manual = manual.count (*it) != 0;
        const bool in_discovery = discovery.count (*it) != 0;
        if (in_manual && in_discovery)
            entry.source = ZLINK_SPOT_PEER_SOURCE_MIXED;
        else if (in_manual)
            entry.source = ZLINK_SPOT_PEER_SOURCE_MANUAL;
        else
            entry.source = ZLINK_SPOT_PEER_SOURCE_DISCOVERY;
        std::map<std::string, zlink_admission_state_t>::const_iterator ait =
          admission_by_endpoint.find (*it);
        entry.admission_state =
          ait != admission_by_endpoint.end () ? ait->second
                                              : ZLINK_ADMISSION_SERVING;

        if (connected.count (*it) != 0)
            entry.state = ZLINK_SPOT_PEER_STATE_CONNECTED;
        else if (active.count (*it) != 0)
            entry.state = ZLINK_SPOT_PEER_STATE_CONNECTING;
        else
            entry.state = ZLINK_SPOT_PEER_STATE_CONFIGURED;
        std::map<std::string, spot_peer_observation_t>::const_iterator oit =
          observations.find (*it);
        if (oit != observations.end ()) {
            entry.connected_since_ms = oit->second.connected_since_ms;
            entry.last_changed_ms = oit->second.last_changed_ms;
        }

        if (spot_peer_filter_match_local (entry, filter_))
            out_->push_back (entry);
    }

    std::sort (out_->begin (), out_->end (), spot_peer_entry_less_local);
    return 0;
}

int spot_node_t::snapshot_subjects (
  const zlink_spot_node_subject_filter_t *filter_,
  std::vector<zlink_spot_node_subject_entry_t> *out_) const
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }
    if (filter_ && filter_->role == ZLINK_SPOT_ROLE_PUB) {
        errno = ENOTSUP;
        return -1;
    }

    out_->clear ();

    uint32_t active_peer_count = 0;
    std::vector<spot_sub_t *> subs;
    std::map<std::string, uint64_t> subject_last_changed;
    {
        scoped_lock_t lock (const_cast<mutex_t &> (_sync));
        active_peer_count = static_cast<uint32_t> (_peer_state.active_endpoints.size ());
        subs.assign (_subs.begin (), _subs.end ());
        subject_last_changed = _subject_last_changed_ms;
    }

    std::map<std::pair<uint32_t, std::string>, zlink_spot_node_subject_entry_t>
      grouped;
    for (size_t i = 0; i < subs.size (); ++i) {
        if (!subs[i])
            continue;
        scoped_lock_t sub_lock (subs[i]->_sync);
        for (std::set<std::string>::const_iterator it = subs[i]->_topics.begin ();
             it != subs[i]->_topics.end (); ++it) {
            const std::pair<uint32_t, std::string> key (
              ZLINK_SERVICE_EVENT_SUBJECT_TOPIC, *it);
            zlink_spot_node_subject_entry_t &entry = grouped[key];
            if (entry.role == 0) {
                memset (&entry, 0, sizeof (entry));
                entry.role = ZLINK_SPOT_ROLE_SUB;
                entry.subject_kind = ZLINK_SERVICE_EVENT_SUBJECT_TOPIC;
                copy_text_field_local (entry.subject, sizeof (entry.subject), *it);
            }
            entry.active_peer_count = active_peer_count;
            if (subs[i]->_ready_subject_endpoints.count (
                  spot_subject_snapshot_key_local (
                    *it, ZLINK_SERVICE_EVENT_SUBJECT_TOPIC))
                != 0) {
                entry.ready_peer_count = 1;
            }
            std::map<std::string, uint64_t>::const_iterator tsit =
              subject_last_changed.find (spot_subject_snapshot_key_local (
                *it, ZLINK_SERVICE_EVENT_SUBJECT_TOPIC));
            if (tsit != subject_last_changed.end ()
                && tsit->second > entry.last_changed_ms) {
                entry.last_changed_ms = tsit->second;
            }
        }
        for (std::set<std::string>::const_iterator it =
               subs[i]->_patterns.begin ();
             it != subs[i]->_patterns.end (); ++it) {
            const std::string pattern = *it + "*";
            const std::pair<uint32_t, std::string> key (
              ZLINK_SERVICE_EVENT_SUBJECT_PATTERN, pattern);
            zlink_spot_node_subject_entry_t &entry = grouped[key];
            if (entry.role == 0) {
                memset (&entry, 0, sizeof (entry));
                entry.role = ZLINK_SPOT_ROLE_SUB;
                entry.subject_kind = ZLINK_SERVICE_EVENT_SUBJECT_PATTERN;
                copy_text_field_local (entry.subject, sizeof (entry.subject),
                                       pattern);
            }
            entry.active_peer_count = active_peer_count;
            if (subs[i]->_ready_subject_endpoints.count (
                  spot_subject_snapshot_key_local (
                    pattern, ZLINK_SERVICE_EVENT_SUBJECT_PATTERN))
                != 0) {
                entry.ready_peer_count = 1;
            }
            std::map<std::string, uint64_t>::const_iterator tsit =
              subject_last_changed.find (spot_subject_snapshot_key_local (
                pattern, ZLINK_SERVICE_EVENT_SUBJECT_PATTERN));
            if (tsit != subject_last_changed.end ()
                && tsit->second > entry.last_changed_ms) {
                entry.last_changed_ms = tsit->second;
            }
        }
    }

    for (std::map<std::pair<uint32_t, std::string>,
                  zlink_spot_node_subject_entry_t>::const_iterator it =
           grouped.begin ();
         it != grouped.end (); ++it) {
        const zlink_spot_node_subject_entry_t &entry = it->second;
        if (filter_) {
            if (filter_->role != 0 && filter_->role != entry.role)
                continue;
            if (filter_->subject_kind != 0
                && filter_->subject_kind != entry.subject_kind) {
                continue;
            }
            if (filter_->subject[0] != '\0'
                && strcmp (filter_->subject, entry.subject) != 0) {
                continue;
            }
        }
        out_->push_back (entry);
    }
    std::sort (out_->begin (), out_->end (), spot_subject_entry_less_local);
    return 0;
}
}
