/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_node.hpp"
#include "services/spot/spot_handle.hpp"
#include "services/spot/spot_internal_receiver.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_runtime.hpp"
#include "services/spot/spot_sub.hpp"

#include "api/service_spot_request_reply_internal.hpp"
#include "core/c_api_copy_internal.hpp"
#include "core/internal_defs.hpp"
#include "sockets/socket_base.hpp"
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
        seed ^= std::hash<std::string> () (key_.subject) + 0x9e3779b9
                + (seed << 6) + (seed >> 2);
        return seed;
    }
};

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

static uint32_t unique_peer_count_local (const std::set<std::string> &manual_,
                                         const std::set<std::string> &discovery_)
{
    const std::set<std::string> *smaller = &manual_;
    const std::set<std::string> *larger = &discovery_;
    if (smaller->size () > larger->size ())
        std::swap (smaller, larger);

    size_t overlap = 0;
    for (std::set<std::string>::const_iterator it = smaller->begin ();
         it != smaller->end (); ++it) {
        if (larger->count (*it) != 0)
            ++overlap;
    }

    return static_cast<uint32_t> (manual_.size () + discovery_.size ()
                                  - overlap);
}

static bool fixed_string_is_nul_terminated (const char *value_, size_t size_)
{
    return value_ && memchr (value_, '\0', size_) != NULL;
}

static zlink_socket_type_t public_socket_type_from_core (int type_)
{
    switch (type_) {
        case ZLINK_CORE_SOCKET_PAIR:
            return ZLINK_SOCKET_PAIR;
        case ZLINK_CORE_SOCKET_PUB:
            return ZLINK_SOCKET_PUB;
        case ZLINK_CORE_SOCKET_SUB:
            return ZLINK_SOCKET_SUB;
        case ZLINK_CORE_SOCKET_DEALER:
            return ZLINK_SOCKET_DEALER;
        case ZLINK_CORE_SOCKET_ROUTER:
            return ZLINK_SOCKET_ROUTER;
        case ZLINK_CORE_SOCKET_XPUB:
            return ZLINK_SOCKET_XPUB;
        case ZLINK_CORE_SOCKET_XSUB:
            return ZLINK_SOCKET_XSUB;
        case ZLINK_CORE_SOCKET_STREAM:
            return ZLINK_SOCKET_STREAM;
        default:
            return ZLINK_SOCKET_ANY;
    }
}

static bool valid_public_socket_type_filter (zlink_socket_type_t type_)
{
    return type_ == ZLINK_SOCKET_ANY || type_ == ZLINK_SOCKET_PAIR
           || type_ == ZLINK_SOCKET_PUB || type_ == ZLINK_SOCKET_SUB
           || type_ == ZLINK_SOCKET_DEALER || type_ == ZLINK_SOCKET_ROUTER
           || type_ == ZLINK_SOCKET_XPUB || type_ == ZLINK_SOCKET_XSUB
           || type_ == ZLINK_SOCKET_STREAM;
}

static int copy_fixed_64 (char *dst_, const char *src_)
{
    const size_t len = src_ ? strlen (src_) : 0;
    if (len >= 64) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memset (dst_, 0, 64);
    if (len != 0)
        memcpy (dst_, src_, len);
    return 0;
}

static bool auto_hwm_visible_from_snapshot (
  const zlink_monitor_snapshot_t &snapshot_)
{
    return snapshot_.auto_hwm_enabled != 0 || snapshot_.auto_hwm_role != 0
           || snapshot_.auto_hwm_applied_sndhwm != 0
           || snapshot_.auto_hwm_applied_rcvhwm != 0;
}

static bool socket_snapshot_matches_filter (
  const zlink_spot_node_socket_snapshot_entry_t &entry_,
  const zlink_spot_node_socket_snapshot_filter_t *filter_)
{
    if (!filter_)
        return true;
    if (filter_->owner != ZLINK_SPOT_NODE_SOCKET_OWNER_ANY
        && filter_->owner != entry_.owner)
        return false;
    if (filter_->socket_type != ZLINK_SOCKET_ANY
        && filter_->socket_type != entry_.socket_type)
        return false;
    if (filter_->socket_name[0] != '\0'
        && strcmp (filter_->socket_name, entry_.socket_name) != 0)
        return false;
    return true;
}

static int append_socket_snapshot_row (
  std::vector<zlink_spot_node_socket_snapshot_entry_t> *out_,
  const zlink_spot_node_socket_snapshot_filter_t *filter_,
  zlink_spot_node_socket_owner_t owner_,
  uint64_t owner_id_,
  const char *owner_name_,
  const char *socket_name_,
  socket_base_t *socket_)
{
    if (!out_ || !socket_)
        return 0;

    zlink_spot_node_socket_snapshot_entry_t entry;
    memset (&entry, 0, sizeof (entry));
    entry.owner = owner_;
    entry.owner_id = owner_id_;
    if (copy_fixed_64 (entry.owner_name, owner_name_) != 0
        || copy_fixed_64 (entry.socket_name, socket_name_) != 0)
        return -1;
    entry.socket_type = public_socket_type_from_core (socket_->socket_type ());
    if (entry.socket_type == ZLINK_SOCKET_ANY) {
        errno = EINVAL;
        return -1;
    }
    if (socket_->monitor_snapshot (&entry.snapshot) != 0)
        return -1;
    entry.auto_hwm_visible = auto_hwm_visible_from_snapshot (entry.snapshot) ? 1u
                                                                             : 0u;
    if (socket_snapshot_matches_filter (entry, filter_))
        out_->push_back (entry);
    return 0;
}
}

std::string spot_node_t::summary_service_name () const
{
    scoped_lock_t lock (_sync);
    if (!_discovery_state.discovery_service.empty ())
        return _discovery_state.discovery_service;
    if (_service_attachment_state.discoveries.size () == 1)
        return _service_attachment_state.discoveries.begin ()->first;
    if (_service_attachment_state.attachments.size () == 1)
        return _service_attachment_state.attachments.begin ()->first;
    return std::string ();
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
        discovery = _discovery_state.discovery;
        service_name = _discovery_state.discovery_service;
        endpoint = _discovery_state.advertise_endpoint.empty () ? _endpoint_state.bound_endpoint : _discovery_state.advertise_endpoint;
    }
    if (!discovery || service_name.empty ())
        return;

    zlink_routing_id_t rid;
    if (pub_->routing_id (&rid) != 0 || rid.size == 0)
        return;

    zlink_registry_topology_entry_t entry;
    memset (&entry, 0, sizeof (entry));
    entry.routing_id = rid;
    entry.auto_connect_type =
      static_cast<zlink_auto_connect_type_t> (discovery->auto_connect_type ());
    entry.service_kind = ZLINK_SERVICE_KIND_SPOT_PUB;
    entry.service_role = ZLINK_SERVICE_ROLE_SPOT;
    copy_fixed_c_string_from_cstr (entry.channel_name,
                                   sizeof (entry.channel_name),
                                   service_name.c_str ());
    copy_fixed_c_string_from_cstr (entry.endpoint, sizeof (entry.endpoint),
                                   endpoint.c_str ());
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
        discovery = _discovery_state.discovery;
        service_name = _discovery_state.discovery_service;
    }
    if (!discovery || service_name.empty ())
        return;

    zlink_routing_id_t rid;
    if (sub_->routing_id (&rid) != 0 || rid.size == 0)
        return;

    zlink_registry_topology_entry_t entry;
    memset (&entry, 0, sizeof (entry));
    entry.routing_id = rid;
    entry.auto_connect_type =
      static_cast<zlink_auto_connect_type_t> (discovery->auto_connect_type ());
    entry.service_kind = ZLINK_SERVICE_KIND_SPOT_SUB;
    entry.service_role = ZLINK_SERVICE_ROLE_SPOT;
    copy_fixed_c_string_from_cstr (entry.channel_name,
                                   sizeof (entry.channel_name),
                                   service_name.c_str ());
    entry.source = ZLINK_TOPOLOGY_SOURCE_DISCOVERY;
    entry.state = static_cast<zlink_topology_state_t> (state_);
    entry.desired_count = 1;
    entry.ready_count = state_ == ZLINK_TOPOLOGY_STATE_READY ? 1 : 0;
    entry.error_code = static_cast<uint32_t> (error_code_ > 0 ? error_code_ : 0);
    entry.last_reported_ms = clock_t ().now_ms ();
    discovery->upsert_service_summary (entry);
}

void spot_node_t::submit_spot_owner_summary_for_rid (
  const zlink_routing_id_t &rid_, uint16_t state_, int error_code_)
{
    if (rid_.size == 0)
        return;

    discovery_t *discovery = NULL;
    std::string service_name;
    std::string endpoint;
    {
        scoped_lock_t lock (_sync);
        discovery = _discovery_state.discovery;
        service_name = _discovery_state.discovery_service;
        endpoint =
          _discovery_state.advertise_endpoint.empty ()
            ? _endpoint_state.bound_endpoint
            : _discovery_state.advertise_endpoint;
    }
    if (!discovery || service_name.empty ())
        return;

    zlink_registry_topology_entry_t entry;
    memset (&entry, 0, sizeof (entry));
    entry.routing_id = rid_;
    entry.auto_connect_type =
      static_cast<zlink_auto_connect_type_t> (discovery->auto_connect_type ());
    entry.service_kind = ZLINK_SERVICE_KIND_SPOT_PUB;
    entry.service_role = ZLINK_SERVICE_ROLE_SPOT;
    copy_fixed_c_string_from_cstr (entry.channel_name,
                                   sizeof (entry.channel_name),
                                   service_name.c_str ());
    copy_fixed_c_string_from_cstr (entry.endpoint, sizeof (entry.endpoint),
                                   endpoint.c_str ());
    entry.source = ZLINK_TOPOLOGY_SOURCE_DISCOVERY;
    entry.state = static_cast<zlink_topology_state_t> (state_);
    entry.desired_count = 1;
    entry.ready_count = state_ == ZLINK_TOPOLOGY_STATE_READY ? 1 : 0;
    entry.error_code = static_cast<uint32_t> (error_code_ > 0 ? error_code_ : 0);
    entry.last_reported_ms = clock_t ().now_ms ();
    discovery->upsert_service_summary (entry);
}

void spot_node_t::submit_spot_owner_summary (
  const std::shared_ptr<spot_logical_state_t> &state_,
  uint16_t state, int error_code_)
{
    if (!state_)
        return;
    submit_spot_owner_summary_for_rid (state_->routing_id, state, error_code_);
}

void spot_node_t::submit_stopped_summaries ()
{
    std::vector<spot_pub_t *> pubs;
    std::vector<spot_sub_t *> subs;
    std::vector<std::shared_ptr<spot_logical_state_t> > spots;
    {
        scoped_lock_t lock (_sync);
        pubs.reserve (_handle_state.pubs.size ());
        subs.reserve (_handle_state.subs.size ());
        pubs.assign (_handle_state.pubs.begin (), _handle_state.pubs.end ());
        subs.assign (_handle_state.subs.begin (), _handle_state.subs.end ());
        spots.reserve (_handle_state.spots_by_rid.size ());
        for (std::map<std::string, std::shared_ptr<spot_logical_state_t> >::
               const_iterator it = _handle_state.spots_by_rid.begin ();
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
    std::vector<std::shared_ptr<spot_logical_state_t> > spots;
    bool bound = false;
    {
        scoped_lock_t lock (_sync);
        pubs.reserve (_handle_state.pubs.size ());
        subs.reserve (_handle_state.subs.size ());
        pubs.assign (_handle_state.pubs.begin (), _handle_state.pubs.end ());
        subs.assign (_handle_state.subs.begin (), _handle_state.subs.end ());
        spots.reserve (_handle_state.spots_by_rid.size ());
        for (std::map<std::string, std::shared_ptr<spot_logical_state_t> >::
               const_iterator it = _handle_state.spots_by_rid.begin ();
             it != _handle_state.spots_by_rid.end (); ++it)
            spots.push_back (it->second);
        bound = !_endpoint_state.bound_endpoint.empty ();
    }

    if (bound) {
        for (size_t i = 0; i < pubs.size (); ++i)
            submit_pub_summary (pubs[i], ZLINK_TOPOLOGY_STATE_READY, 0);
        for (size_t i = 0; i < spots.size (); ++i)
            submit_spot_owner_summary (spots[i], ZLINK_TOPOLOGY_STATE_READY, 0);
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
        subs.reserve (_handle_state.subs.size ());
        subs.assign (_handle_state.subs.begin (), _handle_state.subs.end ());
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

    scoped_lock_t lock (_sync);
    for (std::unordered_map<std::string, uint32_t>::const_iterator it =
           _aggregate_subscriptions.local_exact_topic_refcount.begin ();
         it != _aggregate_subscriptions.local_exact_topic_refcount.end ();
         ++it) {
        if (it->second != 0)
            out_->insert (it->first);
    }
    for (std::unordered_map<std::string, uint32_t>::const_iterator it =
           _aggregate_subscriptions.local_prefix_topic_refcount.begin ();
         it != _aggregate_subscriptions.local_prefix_topic_refcount.end ();
         ++it) {
        if (it->second != 0)
            out_->insert (it->first);
    }
}

bool spot_node_t::update_aggregate_subscription (
  const std::string &raw_filter_, bool pattern_, bool subscribe_)
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

    std::unordered_map<std::string, uint32_t>::iterator it =
      refcounts.find (raw_filter_);
    if (it == refcounts.end () || it->second == 0)
        return false;
    --it->second;
    if (it->second != 0)
        return false;
    refcounts.erase (it);
    return true;
}

int spot_node_t::update_logical_spot_subscription (
  const std::string &raw_filter_, bool pattern_, bool subscribe_)
{
    if (raw_filter_.empty ()) {
        errno = EINVAL;
        return -1;
    }

    const bool changed = update_aggregate_subscription (raw_filter_, pattern_,
                                                        subscribe_);
    if (!changed)
        return 0;

    spot_internal_receiver_t *receiver = ensure_internal_receiver ();
    spot_sub_t *sub = receiver ? receiver->impl () : NULL;
    if (!sub) {
        const bool rollback_changed =
          update_aggregate_subscription (raw_filter_, pattern_, !subscribe_);
        LIBZLINK_UNUSED (rollback_changed);
        errno = ENOTSUP;
        return -1;
    }

    if (sub->apply_aggregate_subscription (raw_filter_, pattern_, subscribe_)
        != 0) {
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
         it != _aggregate_subscriptions.local_exact_topic_refcount.end ();
         ++it) {
        if (it->second == 0)
            continue;
        spot_sub_t::subject_descriptor_t subject;
        subject.subject = it->first;
        subject.subject_kind = ZLINK_SERVICE_EVENT_SUBJECT_TOPIC;
        out_->push_back (subject);
    }
    for (std::unordered_map<std::string, uint32_t>::const_iterator it =
           _aggregate_subscriptions.local_prefix_topic_refcount.begin ();
         it != _aggregate_subscriptions.local_prefix_topic_refcount.end ();
         ++it) {
        if (it->second == 0)
            continue;
        spot_sub_t::subject_descriptor_t subject;
        subject.subject = it->first + "*";
        subject.subject_kind = ZLINK_SERVICE_EVENT_SUBJECT_PATTERN;
        out_->push_back (subject);
    }
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
        scoped_lock_t lock (_sync);
        service_name = _discovery_state.discovery_service;
        local_endpoint =
          !_discovery_state.advertise_endpoint.empty () ? _discovery_state.advertise_endpoint : _endpoint_state.bound_endpoint;
        out_->configured_peer_count = unique_peer_count_local (
          _peer_state.manual_endpoints, _peer_state.discovery_endpoints);
        out_->active_peer_count =
          static_cast<uint32_t> (_peer_state.active_endpoints.size ());
        out_->connected_peer_count =
          static_cast<uint32_t> (_peer_state.connected_endpoints.size ());
        out_->last_error = _summary_state.last_summary_error;
        if (_runtime && _runtime->faulted)
            out_->last_error = _runtime->fault_errno;
        out_->disconnected_sub_target_count = 0;
        out_->disconnected_routed_target_count = 0;
        out_->last_changed_ms = _summary_state.summary_last_changed_ms;
    }

    copy_fixed_c_string_from_bytes (out_->service_name,
                                    sizeof (out_->service_name),
                                    service_name.data (), service_name.size ());
    copy_fixed_c_string_from_bytes (out_->local_endpoint,
                                    sizeof (out_->local_endpoint),
                                    local_endpoint.data (),
                                    local_endpoint.size ());

    (void) node_routing_id (&out_->node_routing_id);

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
    std::map<std::string, uint32_t> weight_by_endpoint;
    {
        scoped_lock_t lock (_sync);
        service_name = _discovery_state.discovery_service;
        local_endpoint =
          !_discovery_state.advertise_endpoint.empty () ? _discovery_state.advertise_endpoint : _endpoint_state.bound_endpoint;
        manual = _peer_state.manual_endpoints;
        discovery = _peer_state.discovery_endpoints;
        active = _peer_state.active_endpoints;
        connected = _peer_state.connected_endpoints;
        observations = _peer_state.observations;
        weight_by_endpoint = _peer_state.peer_weight_by_endpoint;
    }

    out_->reserve (manual.size () + discovery.size ());
    for (std::set<std::string>::const_iterator it = manual.begin ();
         it != manual.end (); ++it) {
        zlink_spot_node_peer_entry_t entry;
        memset (&entry, 0, sizeof (entry));
        copy_fixed_c_string_from_bytes (entry.service_name,
                                        sizeof (entry.service_name),
                                        service_name.data (),
                                        service_name.size ());
        copy_fixed_c_string_from_bytes (entry.local_endpoint,
                                        sizeof (entry.local_endpoint),
                                        local_endpoint.data (),
                                        local_endpoint.size ());
        copy_fixed_c_string_from_bytes (entry.peer_endpoint,
                                        sizeof (entry.peer_endpoint), it->data (),
                                        it->size ());
        const bool in_manual = manual.count (*it) != 0;
        const bool in_discovery = discovery.count (*it) != 0;
        if (in_manual && in_discovery)
            entry.source = ZLINK_SPOT_PEER_SOURCE_MIXED;
        else if (in_manual)
            entry.source = ZLINK_SPOT_PEER_SOURCE_MANUAL;
        else
            entry.source = ZLINK_SPOT_PEER_SOURCE_DISCOVERY;
        std::map<std::string, uint32_t>::const_iterator ait =
          weight_by_endpoint.find (*it);
        entry.weight =
          ait != weight_by_endpoint.end () ? ait->second
                                              : 100;

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
    for (std::set<std::string>::const_iterator it = discovery.begin ();
         it != discovery.end (); ++it) {
        if (manual.count (*it) != 0)
            continue;

        zlink_spot_node_peer_entry_t entry;
        memset (&entry, 0, sizeof (entry));
        copy_fixed_c_string_from_bytes (entry.service_name,
                                        sizeof (entry.service_name),
                                        service_name.data (),
                                        service_name.size ());
        copy_fixed_c_string_from_bytes (entry.local_endpoint,
                                        sizeof (entry.local_endpoint),
                                        local_endpoint.data (),
                                        local_endpoint.size ());
        copy_fixed_c_string_from_bytes (entry.peer_endpoint,
                                        sizeof (entry.peer_endpoint), it->data (),
                                        it->size ());
        entry.source = ZLINK_SPOT_PEER_SOURCE_DISCOVERY;
        std::map<std::string, uint32_t>::const_iterator ait =
          weight_by_endpoint.find (*it);
        entry.weight =
          ait != weight_by_endpoint.end () ? ait->second
                                              : 100;

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
        scoped_lock_t lock (_sync);
        active_peer_count = static_cast<uint32_t> (_peer_state.active_endpoints.size ());
        subs.reserve (_handle_state.subs.size ());
        subs.assign (_handle_state.subs.begin (), _handle_state.subs.end ());
        subject_last_changed = _summary_state.subject_last_changed_ms;
    }

    std::unordered_map<subject_snapshot_key_t,
                       zlink_spot_node_subject_entry_t,
                       subject_snapshot_key_hash_t>
      grouped;
    grouped.reserve (subs.size () * 2);
    for (size_t i = 0; i < subs.size (); ++i) {
        if (!subs[i])
            continue;
        scoped_lock_t sub_lock (subs[i]->_sync);
        for (std::set<std::string>::const_iterator it = subs[i]->_topics.begin ();
             it != subs[i]->_topics.end (); ++it) {
            subject_snapshot_key_t key;
            key.subject_kind = ZLINK_SERVICE_EVENT_SUBJECT_TOPIC;
            key.subject = *it;
            zlink_spot_node_subject_entry_t &entry = grouped[key];
            if (entry.role == 0) {
                memset (&entry, 0, sizeof (entry));
                entry.role = ZLINK_SPOT_ROLE_SUB;
                entry.subject_kind = ZLINK_SERVICE_EVENT_SUBJECT_TOPIC;
                copy_fixed_c_string_from_bytes (entry.subject,
                                                sizeof (entry.subject),
                                                it->data (), it->size ());
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
            subject_snapshot_key_t key;
            key.subject_kind = ZLINK_SERVICE_EVENT_SUBJECT_PATTERN;
            key.subject = pattern;
            zlink_spot_node_subject_entry_t &entry = grouped[key];
            if (entry.role == 0) {
                memset (&entry, 0, sizeof (entry));
                entry.role = ZLINK_SPOT_ROLE_SUB;
                entry.subject_kind = ZLINK_SERVICE_EVENT_SUBJECT_PATTERN;
                copy_fixed_c_string_from_bytes (entry.subject,
                                                sizeof (entry.subject),
                                                pattern.data (),
                                                pattern.size ());
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

    out_->reserve (grouped.size ());
    for (std::unordered_map<subject_snapshot_key_t,
                            zlink_spot_node_subject_entry_t,
                            subject_snapshot_key_hash_t>::const_iterator it =
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

int spot_node_t::snapshot_internal_sockets (
  const zlink_spot_node_socket_snapshot_filter_t *filter_,
  std::vector<zlink_spot_node_socket_snapshot_entry_t> *out_) const
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }
    if (filter_) {
        if (filter_->owner != ZLINK_SPOT_NODE_SOCKET_OWNER_ANY
            && filter_->owner != ZLINK_SPOT_NODE_SOCKET_OWNER_NODE
            && filter_->owner != ZLINK_SPOT_NODE_SOCKET_OWNER_SPOT) {
            errno = EINVAL;
            return -1;
        }
        if (!valid_public_socket_type_filter (filter_->socket_type)
            || !fixed_string_is_nul_terminated (
                 filter_->socket_name, sizeof (filter_->socket_name))) {
            errno = EINVAL;
            return -1;
        }
    }

    out_->clear ();
    scoped_lock_t lock (_sync);
    if (_runtime) {
        if (append_socket_snapshot_row (
              out_, filter_, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE, 0, "spotnode",
              "mesh-pub", _runtime->mesh_pub)
            != 0
            || append_socket_snapshot_row (
                 out_, filter_, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE, 0,
                 "spotnode", "mesh-xsub", _runtime->mesh_xsub)
                 != 0
            || append_socket_snapshot_row (
                 out_, filter_, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE, 0,
                 "spotnode", "peer_ctrl_pub", _runtime->peer_ctrl_pub)
                 != 0
            || append_socket_snapshot_row (
                 out_, filter_, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE, 0,
                 "spotnode", "peer_ctrl_sub", _runtime->peer_ctrl_sub)
                 != 0
            || append_socket_snapshot_row (
                 out_, filter_, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE, 0,
                 "spotnode", "external-router",
                 _runtime->external_router)
                 != 0
            || append_socket_snapshot_row (
                 out_, filter_, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE, 0,
                 "spotnode", "internal-router", _runtime->internal_router)
                 != 0
            || append_socket_snapshot_row (
                 out_, filter_, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE, 0,
                 "spotnode", "internal-router-tx", _runtime->internal_router_tx)
                 != 0
            || append_socket_snapshot_row (
                 out_, filter_, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE, 0,
                 "spotnode", "pub-ingress-tx", _runtime->pub_ingress_tx)
                 != 0
            || append_socket_snapshot_row (
                 out_, filter_, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE, 0,
                 "spotnode", "ingress-sub",
                 _runtime->local_pub_ingress_sub)
                 != 0
            || append_socket_snapshot_row (
                 out_, filter_, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE, 0,
                 "spotnode", "local-pub", _runtime->local_fanout_xpub)
                 != 0)
            return -1;
    }

    if (append_socket_snapshot_row (
          out_, filter_, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE, 0, "spotnode",
          "internal_receiver",
          _handle_state.handle_defaults.internal_receiver ()
            ? _handle_state.handle_defaults.internal_receiver ()
                ->snapshot_socket ()
            : NULL)
        != 0)
        return -1;

    return 0;
}
}
