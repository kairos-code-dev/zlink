/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "core/c_api_copy_internal.hpp"
#include "core/internal_defs.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/dispatch/spot_internal_receiver.hpp"
#include "services/spot/runtime/spot_runtime.hpp"
#include "sockets/common/socket_base.hpp"

#include <algorithm>
#include <cstring>

namespace zlink
{
namespace
{

bool spot_peer_filter_match_local (const zlink_spot_node_peer_entry_t &entry_,
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

bool spot_peer_entry_less_local (const zlink_spot_node_peer_entry_t &lhs_,
                                 const zlink_spot_node_peer_entry_t &rhs_)
{
    return strcmp (lhs_.peer_endpoint, rhs_.peer_endpoint) < 0;
}

bool spot_subject_entry_less_local (const zlink_spot_node_subject_entry_t &lhs_,
                                    const zlink_spot_node_subject_entry_t &rhs_)
{
    if (lhs_.subject_kind != rhs_.subject_kind)
        return lhs_.subject_kind < rhs_.subject_kind;
    return strcmp (lhs_.subject, rhs_.subject) < 0;
}

uint32_t unique_peer_count_local (const std::set<std::string> &manual_,
                                  const std::set<std::string> &discovery_)
{
    const std::set<std::string> *smaller = &manual_;
    const std::set<std::string> *larger = &discovery_;
    if (smaller->size () > larger->size ())
        std::swap (smaller, larger);

    size_t overlap = 0;
    for (std::set<std::string>::const_iterator it = smaller->begin (); it != smaller->end ();
         ++it) {
        if (larger->count (*it) != 0)
            ++overlap;
    }

    return static_cast<uint32_t> (manual_.size () + discovery_.size () - overlap);
}

bool fixed_string_is_nul_terminated (const char *value_, size_t size_)
{
    return value_ && memchr (value_, '\0', size_) != NULL;
}

zlink_socket_type_t public_socket_type_from_core (int type_)
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

bool valid_public_socket_type_filter (zlink_socket_type_t type_)
{
    return type_ == ZLINK_SOCKET_ANY || type_ == ZLINK_SOCKET_PAIR || type_ == ZLINK_SOCKET_PUB
           || type_ == ZLINK_SOCKET_SUB || type_ == ZLINK_SOCKET_DEALER
           || type_ == ZLINK_SOCKET_ROUTER || type_ == ZLINK_SOCKET_XPUB
           || type_ == ZLINK_SOCKET_XSUB || type_ == ZLINK_SOCKET_STREAM;
}

int copy_fixed_64 (char *dst_, const char *src_)
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

bool auto_hwm_visible_from_snapshot (const zlink_monitor_status_t &snapshot_)
{
    return snapshot_.auto_hwm_enabled != 0 || snapshot_.auto_hwm_role != 0
           || snapshot_.auto_hwm_applied_sndhwm != 0 || snapshot_.auto_hwm_applied_rcvhwm != 0;
}

bool socket_snapshot_matches_filter (const zlink_spot_node_socket_entry_t &entry_,
                                     const zlink_spot_node_socket_filter_t *filter_)
{
    if (!filter_)
        return true;
    if (filter_->owner != ZLINK_SPOT_NODE_SOCKET_OWNER_ANY && filter_->owner != entry_.owner)
        return false;
    if (filter_->socket_type != ZLINK_SOCKET_ANY && filter_->socket_type != entry_.socket_type)
        return false;
    if (filter_->socket_name[0] != '\0' && strcmp (filter_->socket_name, entry_.socket_name) != 0)
        return false;
    return true;
}

int append_socket_snapshot_row (std::vector<zlink_spot_node_socket_entry_t> *out_,
                                const zlink_spot_node_socket_filter_t *filter_,
                                zlink_spot_node_socket_owner_t owner_,
                                uint64_t owner_id_,
                                const char *owner_name_,
                                const char *socket_name_,
                                socket_base_t *socket_)
{
    if (!out_ || !socket_)
        return 0;

    zlink_spot_node_socket_entry_t entry;
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
    if (socket_->monitor_snapshot (&entry.monitor_status) != 0)
        return -1;
    entry.auto_hwm_visible = auto_hwm_visible_from_snapshot (entry.monitor_status) ? 1u : 0u;
    if (socket_snapshot_matches_filter (entry, filter_))
        out_->push_back (entry);
    return 0;
}

}

int spot_node_t::snapshot_status (zlink_spot_node_status_t *out_)
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }

    memset (out_, 0, sizeof (*out_));

    std::string channel_name;
    std::string local_endpoint;
    zlink_routing_id_t node_rid;
    memset (&node_rid, 0, sizeof (node_rid));
    {
        scoped_lock_t lock (_sync);
        channel_name = _discovery_state.discovery_service;
        local_endpoint = !_discovery_state.advertise_endpoint.empty ()
                           ? _discovery_state.advertise_endpoint
                           : _endpoint_state.bound_endpoint;
        out_->configured_peer_count =
          unique_peer_count_local (_peer_state.manual_endpoints, _peer_state.discovery_endpoints);
        out_->active_peer_count = static_cast<uint32_t> (_peer_state.active_endpoints.size ());
        out_->connected_peer_count =
          static_cast<uint32_t> (_peer_state.connected_endpoints.size ());
        out_->last_error = _summary_state.last_summary_error;
        if (_runtime && _runtime->faulted)
            out_->last_error = _runtime->fault_errno;
        out_->disconnected_sub_target_count = 0;
        out_->disconnected_routed_target_count = 0;
        out_->last_changed_ms = _summary_state.summary_last_changed_ms;
        node_rid = _node_routing_id;
    }

    copy_fixed_c_string_from_bytes (out_->channel_name, sizeof (out_->channel_name),
                                    channel_name.data (), channel_name.size ());
    copy_fixed_c_string_from_bytes (out_->local_endpoint, sizeof (out_->local_endpoint),
                                    local_endpoint.data (), local_endpoint.size ());

    out_->node_routing_id = node_rid;
    snapshot_status_subject_counts (&out_->subject_count, &out_->ready_subject_count);

    if (out_->last_error != 0)
        out_->state = ZLINK_SPOT_NODE_STATE_ERROR;
    else if (out_->configured_peer_count == 0 && out_->subject_count == 0)
        out_->state = ZLINK_SPOT_NODE_STATE_IDLE;
    else if (out_->active_peer_count > 0 && out_->connected_peer_count == 0)
        out_->state = ZLINK_SPOT_NODE_STATE_CONNECTING;
    else if (out_->connected_peer_count > 0 && out_->ready_subject_count < out_->subject_count) {
        out_->state = ZLINK_SPOT_NODE_STATE_PARTIAL_READY;
    } else if (out_->connected_peer_count > 0 && out_->subject_count > 0
               && out_->ready_subject_count == out_->subject_count) {
        out_->state = ZLINK_SPOT_NODE_STATE_READY;
    } else
        out_->state = ZLINK_SPOT_NODE_STATE_IDLE;

    return 0;
}

int spot_node_t::snapshot_peers (const zlink_spot_node_peer_filter_t *filter_,
                                 std::vector<zlink_spot_node_peer_entry_t> *out_) const
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }
    out_->clear ();

    std::string channel_name;
    std::string local_endpoint;
    std::set<std::string> manual;
    std::set<std::string> discovery;
    std::set<std::string> active;
    std::set<std::string> connected;
    std::map<std::string, spot_peer_observation_t> observations;
    std::map<std::string, uint32_t> weight_by_endpoint;
    {
        scoped_lock_t lock (_sync);
        channel_name = _discovery_state.discovery_service;
        local_endpoint = !_discovery_state.advertise_endpoint.empty ()
                           ? _discovery_state.advertise_endpoint
                           : _endpoint_state.bound_endpoint;
        manual = _peer_state.manual_endpoints;
        discovery = _peer_state.discovery_endpoints;
        active = _peer_state.active_endpoints;
        connected = _peer_state.connected_endpoints;
        observations = _peer_state.observations;
        weight_by_endpoint = _peer_state.peer_weight_by_endpoint;
    }

    out_->reserve (manual.size () + discovery.size ());
    for (std::set<std::string>::const_iterator it = manual.begin (); it != manual.end (); ++it) {
        zlink_spot_node_peer_entry_t entry;
        memset (&entry, 0, sizeof (entry));
        copy_fixed_c_string_from_bytes (entry.channel_name, sizeof (entry.channel_name),
                                        channel_name.data (), channel_name.size ());
        copy_fixed_c_string_from_bytes (entry.local_endpoint, sizeof (entry.local_endpoint),
                                        local_endpoint.data (), local_endpoint.size ());
        copy_fixed_c_string_from_bytes (entry.peer_endpoint, sizeof (entry.peer_endpoint),
                                        it->data (), it->size ());
        const bool in_manual = manual.count (*it) != 0;
        const bool in_discovery = discovery.count (*it) != 0;
        if (in_manual && in_discovery)
            entry.source = ZLINK_SPOT_PEER_SOURCE_MIXED;
        else if (in_manual)
            entry.source = ZLINK_SPOT_PEER_SOURCE_MANUAL;
        else
            entry.source = ZLINK_SPOT_PEER_SOURCE_DISCOVERY;
        entry.kind = ZLINK_SPOT_PEER_KIND_SPOT_MESH;
        std::map<std::string, uint32_t>::const_iterator ait = weight_by_endpoint.find (*it);
        entry.weight = ait != weight_by_endpoint.end () ? ait->second : 100;

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
    for (std::set<std::string>::const_iterator it = discovery.begin (); it != discovery.end ();
         ++it) {
        if (manual.count (*it) != 0)
            continue;

        zlink_spot_node_peer_entry_t entry;
        memset (&entry, 0, sizeof (entry));
        copy_fixed_c_string_from_bytes (entry.channel_name, sizeof (entry.channel_name),
                                        channel_name.data (), channel_name.size ());
        copy_fixed_c_string_from_bytes (entry.local_endpoint, sizeof (entry.local_endpoint),
                                        local_endpoint.data (), local_endpoint.size ());
        copy_fixed_c_string_from_bytes (entry.peer_endpoint, sizeof (entry.peer_endpoint),
                                        it->data (), it->size ());
        entry.source = ZLINK_SPOT_PEER_SOURCE_DISCOVERY;
        entry.kind = ZLINK_SPOT_PEER_KIND_SPOT_MESH;
        std::map<std::string, uint32_t>::const_iterator ait = weight_by_endpoint.find (*it);
        entry.weight = ait != weight_by_endpoint.end () ? ait->second : 100;

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

int spot_node_t::snapshot_subjects (const zlink_spot_node_subject_filter_t *filter_,
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
    {
        scoped_lock_t lock (_sync);
        active_peer_count = static_cast<uint32_t> (_peer_state.active_endpoints.size ());
    }

    std::vector<spot_node_summary_state_t::subject_snapshot_entry_t> snapshots;
    snapshot_subject_summary_entries (&snapshots);

    out_->reserve (snapshots.size ());
    for (size_t i = 0; i < snapshots.size (); ++i) {
        const spot_node_summary_state_t::subject_snapshot_entry_t &status = snapshots[i];
        zlink_spot_node_subject_entry_t entry;
        memset (&entry, 0, sizeof (entry));
        entry.role = ZLINK_SPOT_ROLE_SUB;
        entry.subject_kind = status.subject_kind;
        entry.ready_peer_count = status.ready ? 1 : 0;
        entry.active_peer_count = active_peer_count;
        entry.last_changed_ms = status.last_changed_ms;
        copy_fixed_c_string_from_bytes (entry.subject, sizeof (entry.subject),
                                        status.subject.data (), status.subject.size ());
        if (filter_) {
            if (filter_->role != 0 && filter_->role != entry.role)
                continue;
            if (filter_->subject_kind != 0 && filter_->subject_kind != entry.subject_kind) {
                continue;
            }
            if (filter_->subject[0] != '\0' && strcmp (filter_->subject, entry.subject) != 0) {
                continue;
            }
        }
        out_->push_back (entry);
    }
    std::sort (out_->begin (), out_->end (), spot_subject_entry_less_local);
    return 0;
}

int spot_node_t::snapshot_internal_sockets (const zlink_spot_node_socket_filter_t *filter_,
                                            std::vector<zlink_spot_node_socket_entry_t> *out_) const
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
            || !fixed_string_is_nul_terminated (filter_->socket_name,
                                                sizeof (filter_->socket_name))) {
            errno = EINVAL;
            return -1;
        }
    }

    out_->clear ();
    scoped_lock_t lock (_sync);
    if (_runtime) {
        if (append_socket_snapshot_row (out_, filter_, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE, 0,
                                        "spotnode", "mesh-pub", _runtime->mesh_pub)
              != 0
            || append_socket_snapshot_row (out_, filter_, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE, 0,
                                           "spotnode", "mesh-xsub", _runtime->mesh_xsub)
                 != 0
            || append_socket_snapshot_row (out_, filter_, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE, 0,
                                           "spotnode", "peer_ctrl_pub", _runtime->peer_ctrl_pub)
                 != 0
            || append_socket_snapshot_row (out_, filter_, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE, 0,
                                           "spotnode", "peer_ctrl_sub", _runtime->peer_ctrl_sub)
                 != 0
            || append_socket_snapshot_row (out_, filter_, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE, 0,
                                           "spotnode", "routed-router", _runtime->routed_router)
                 != 0
            || append_socket_snapshot_row (out_, filter_, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE, 0,
                                           "spotnode", "local-pub", _runtime->local_fanout_xpub)
                 != 0)
            return -1;
    }

    if (append_socket_snapshot_row (
          out_, filter_, ZLINK_SPOT_NODE_SOCKET_OWNER_NODE, 0, "spotnode", "internal_receiver",
          _handle_state.handle_defaults.internal_receiver ()
            ? _handle_state.handle_defaults.internal_receiver ()->snapshot_socket ()
            : NULL)
        != 0)
        return -1;

    return 0;
}

}
