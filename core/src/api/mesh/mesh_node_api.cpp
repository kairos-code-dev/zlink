/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/mesh/mesh_c_internal.hpp"
#include "services/mesh/mesh_wire.hpp"
#include "api/mesh/mesh_api_internal.hpp"

#include "core/ctx.hpp"
#include "utils/err.hpp"
#include "utils/macros.hpp"

#include <string.h>
#include <algorithm>

using namespace zlink::mesh;

namespace zlink
{
namespace mesh
{
int check_name (const char *name_, size_t max_, std::string *out_)
{
    if (!name_) {
        errno = EINVAL;
        return -1;
    }
    const size_t len = strlen (name_);
    if (len == 0 || len > max_) {
        errno = EINVAL;
        return -1;
    }
    if (out_)
        out_->assign (name_, len);
    return 0;
}

owner_id_t node_owner ()
{
    owner_id_t id;
    id.kind = owner_node;
    id.generation = 0;
    return id;
}

owner_id_t spot_owner (const rid_bytes_t &rid_, uint64_t generation_)
{
    owner_id_t id;
    id.kind = owner_spot;
    id.key.assign (rid_.begin (), rid_.end ());
    id.generation = generation_;
    return id;
}

owner_id_t actor_owner (const std::string &id_, uint64_t generation_)
{
    owner_id_t id;
    id.kind = owner_actor;
    id.key = id_;
    id.generation = generation_;
    return id;
}

zlink_mesh_operation_id_t register_operation (mesh_node_t *node_,
                                              zlink_mesh_operation_kind_t kind_,
                                              const owner_id_t &requester_,
                                              uint32_t timeout_ms_)
{
    std::lock_guard<std::mutex> lock (node_->mutex);
    pending_operation_t op;
    memset (&op.join_actor, 0, sizeof (op.join_actor));
    op.id.high = node_->lifecycle_generation;
    op.id.low = node_->next_operation_serial++;
    op.kind = kind_;
    op.requester = requester_;
    op.deadline_ms = timeout_ms_ > 0 ? now_ms () + timeout_ms_ : 0;
    node_->operations[op.id.low] = op;
    return op.id;
}
}
}

namespace
{
//  Creates a logical Spot record and its owner mailboxes. Caller holds the
//  node mutex.
spot_state_t &create_spot_locked (mesh_node_t *node_,
                                  const rid_bytes_t &rid_,
                                  zlink_spot_kind_t kind_)
{
    const std::string key (rid_.begin (), rid_.end ());
    spot_state_t &spot = node_->spots[key];
    spot.rid = rid_;
    spot.generation = node_->next_spot_generation++;
    spot.kind = kind_;
    spot.last_changed_ms = now_ms ();

    owner_state_t &owner = node_->owners[spot_owner (rid_, spot.generation)];
    owner.id = spot_owner (rid_, spot.generation);
    owner.spot_rid = rid_value (rid_);
    return spot;
}

spot_facade_t *new_facade (mesh_node_t *node_, spot_state_t &spot_)
{
    spot_facade_t *facade = new (std::nothrow) spot_facade_t ();
    if (!facade) {
        errno = ENOMEM;
        return NULL;
    }
    facade->node = node_;
    facade->spot_rid = spot_.rid;
    facade->generation = spot_.generation;
    spot_.facade_count += 1;
    track_facade (facade, true);
    return facade;
}


void emit_state_changed (mesh_node_t *node_)
{
    zlink_mesh_monitor_event_t event;
    memset (&event, 0, sizeof (event));
    event.kind = ZLINK_MESH_MONITOR_STATE_CHANGED;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        event.mesh_state = node_->state;
        event.mesh_lifecycle_generation = node_->lifecycle_generation;
    }
    emit_monitor_event (node_, event);
}
}

//  --- lifecycle -------------------------------------------------------------

void *zlink_mesh_node_new (void *ctx_, const zlink_mesh_node_options_t *options_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    if (check_versioned (options_) != 0)
        return NULL;
    if (!options_->mesh_name || options_->mesh_name_size == 0
        || options_->mesh_name_size > ZLINK_MESH_NAME_MAX) {
        errno = EINVAL;
        return NULL;
    }
    if (memchr (options_->mesh_name, 0, options_->mesh_name_size)) {
        errno = EINVAL;
        return NULL;
    }
    if (options_->trust_profile_size > ZLINK_MESH_NAME_MAX
        || (options_->trust_profile_size > 0 && !options_->trust_profile)) {
        errno = EINVAL;
        return NULL;
    }

    mesh_node_t *node = new (std::nothrow) mesh_node_t (static_cast<zlink::ctx_t *> (ctx_));
    if (!node) {
        errno = ENOMEM;
        return NULL;
    }
    node->mesh_name.assign (options_->mesh_name, options_->mesh_name_size);
    if (options_->trust_profile_size > 0)
        node->trust_profile.assign (options_->trust_profile, options_->trust_profile_size);

    if (register_node (node) != 0) {
        delete node;
        errno = EEXIST;
        return NULL;
    }
    return node;
}

zlink_config_result_t zlink_mesh_node_set_bind (void *mesh_node_, const char *endpoint_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    std::string endpoint;
    if (check_name (endpoint_, ZLINK_MESH_ENDPOINT_MAX, &endpoint) != 0)
        return ZLINK_CONFIG_INVALID_ARGUMENT;

    std::lock_guard<std::mutex> lock (node->mutex);
    if (node->state != ZLINK_MESH_NODE_CREATED) {
        errno = EBUSY;
        return ZLINK_CONFIG_INVALID_STATE;
    }
    node->bind_endpoint = endpoint;
    return ZLINK_CONFIG_OK;
}

zlink_config_result_t zlink_mesh_node_add_channel_name (void *mesh_node_, const char *channel_name_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    std::string name;
    if (check_name (channel_name_, ZLINK_CHANNEL_NAME_MAX, &name) != 0)
        return ZLINK_CONFIG_INVALID_ARGUMENT;

    std::lock_guard<std::mutex> lock (node->mutex);
    if (node->state != ZLINK_MESH_NODE_CREATED) {
        errno = EBUSY;
        return ZLINK_CONFIG_INVALID_STATE;
    }
    if (node->channels.count (name)) {
        errno = EEXIST;
        return ZLINK_CONFIG_CONFLICT;
    }
    node->channels[name] = 100;
    return ZLINK_CONFIG_OK;
}

zlink_config_result_t
zlink_mesh_node_set_channel_weight (void *mesh_node_, const char *channel_name_, uint32_t weight_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    std::string name;
    if (check_name (channel_name_, ZLINK_CHANNEL_NAME_MAX, &name) != 0)
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    if (weight_ > 100) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }

    bool changed = false;
    uint64_t revision = 0;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        std::map<std::string, uint32_t>::iterator it = node->channels.find (name);
        if (it == node->channels.end ()) {
            errno = ENOENT;
            return ZLINK_CONFIG_NOT_FOUND;
        }
        if (it->second != weight_) {
            it->second = weight_;
            node->descriptor_revision += 1;
            revision = node->descriptor_revision;
            wire_broadcast_update_locked (node);
            changed = true;
        }
    }
    if (changed) {
        zlink_mesh_monitor_event_t event;
        memset (&event, 0, sizeof (event));
        event.kind = ZLINK_MESH_MONITOR_CHANNEL_CHANGED;
        event.mesh_descriptor_revision = revision;
        snprintf (event.channel_name, sizeof (event.channel_name), "%s", name.c_str ());
        emit_monitor_event (node, event);
    }
    return ZLINK_CONFIG_OK;
}

zlink_config_result_t zlink_mesh_node_start (void *mesh_node_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }

    {
        std::lock_guard<std::mutex> lock (node->mutex);
        if (node->state != ZLINK_MESH_NODE_CREATED) {
            errno = EBUSY;
            return ZLINK_CONFIG_INVALID_STATE;
        }
        if (node->routing_id.empty () || node->bind_endpoint.empty () || node->channels.empty ()) {
            errno = EINVAL;
            return ZLINK_CONFIG_INVALID_STATE;
        }
    }

    //  Bind the wire first: a failed bind must leave the node CREATED.
    //  Configuration is immutable from here (spec: start is not concurrent
    //  with configure), so the wire reads it without the mutex.
    if (wire_start (node) != 0)
        return ZLINK_CONFIG_INVALID_STATE;

    {
        std::lock_guard<std::mutex> lock (node->mutex);

        //  Node owner mailboxes.
        node->owners[node_owner ()].id = node_owner ();

        //  Entry Spot shares the node routing id.
        create_spot_locked (node, node->routing_id, ZLINK_SPOT_KIND_ENTRY);

        node->state = ZLINK_MESH_NODE_STARTED;
        node->descriptor_revision = 1;
        node->last_changed_ms = now_ms ();
        recompute_readiness_locked (node);
    }
    emit_state_changed (node);
    return ZLINK_CONFIG_OK;
}

zlink_request_result_t zlink_mesh_node_shutdown (void *mesh_node_, uint32_t timeout_ms_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }

    std::unique_lock<std::mutex> lock (node->mutex);
    if (node->state == ZLINK_MESH_NODE_DRAINING) {
        errno = EDEADLK;
        return ZLINK_REQUEST_INVALID_STATE;
    }
    if (node->state == ZLINK_MESH_NODE_STOPPED)
        return ZLINK_REQUEST_OK;

    node->state = ZLINK_MESH_NODE_DRAINING;
    node->last_changed_ms = now_ms ();
    node->cv.notify_all ();

    //  Fail outstanding operations that cannot complete once new admission
    //  has stopped: they terminate with ESHUTDOWN at the deadline.
    const uint64_t deadline = timeout_ms_ > 0 ? now_ms () + timeout_ms_ : now_ms ();

    bool drained = false;
    while (true) {
        bool active_claims = false;
        for (std::map<owner_id_t, owner_state_t>::iterator it = node->owners.begin ();
             it != node->owners.end (); ++it) {
            if (it->second.domains[0].claimed || it->second.domains[1].claimed)
                active_claims = true;
        }
        const bool operations_pending = !node->operations.empty ();
        if (!active_claims && !operations_pending) {
            drained = true;
            break;
        }
        const uint64_t now = now_ms ();
        if (timeout_ms_ == 0 || now >= deadline)
            break;
        node->cv.wait_for (lock, std::chrono::milliseconds (deadline - now));
    }

    if (!drained) {
        //  Revoke outstanding claims: release stays safe, recv fails.
        std::vector<zlink_mesh_monitor_event_t> revoked_events;
        for (std::map<owner_id_t, owner_state_t>::iterator it = node->owners.begin ();
             it != node->owners.end (); ++it) {
            for (int d = 0; d < 2; ++d) {
                if (it->second.domains[d].claimed) {
                    it->second.domains[d].revoked = true;
                    zlink_mesh_monitor_event_t event;
                    memset (&event, 0, sizeof (event));
                    event.kind = ZLINK_MESH_MONITOR_CLAIM_REVOKED;
                    event.mesh_lifecycle_generation = node->lifecycle_generation;
                    event.owner_kind =
                      static_cast<zlink_mesh_owner_kind_t> (it->second.id.kind);
                    event.spot_rid = it->second.spot_rid;
                    event.actor = it->second.actor;
                    revoked_events.push_back (event);
                }
            }
        }
        lock.unlock ();
        for (size_t i = 0; i < revoked_events.size (); ++i)
            emit_monitor_event (node, revoked_events[i]);
        errno = ETIMEDOUT;
        return ZLINK_REQUEST_TIMED_OUT;
    }

    node->state = ZLINK_MESH_NODE_STOPPED;
    node->last_changed_ms = now_ms ();
    lock.unlock ();
    wire_stop (node);
    emit_state_changed (node);
    return ZLINK_REQUEST_OK;
}

zlink_close_result_t zlink_mesh_node_destroy (void **mesh_node_p_)
{
    if (!mesh_node_p_) {
        errno = EFAULT;
        return ZLINK_CLOSE_INVALID_HANDLE;
    }
    mesh_node_t *node = as_mesh_node (*mesh_node_p_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CLOSE_INVALID_HANDLE;
    }

    {
        std::lock_guard<std::mutex> lock (node->mutex);
        uint32_t live_facades = 0;
        uint32_t live_timers = 0;
        for (std::map<std::string, spot_state_t>::iterator it = node->spots.begin ();
             it != node->spots.end (); ++it) {
            live_facades += it->second.facade_count;
            live_timers += it->second.timer_count;
        }
        if (node->publisher_count > 0 || node->monitor_count > 0
            || node->stream_session_count > 0 || live_facades > 0 || live_timers > 0) {
            errno = EBUSY;
            return ZLINK_CLOSE_BUSY;
        }
    }

    //  Forced termination of any non-stopped node: outstanding operations end
    //  with an ESHUTDOWN terminal completion before storage goes away.
    {
        std::unique_lock<std::mutex> lock (node->mutex);
        if (node->state != ZLINK_MESH_NODE_STOPPED) {
            node->state = ZLINK_MESH_NODE_STOPPED;
            node->last_changed_ms = now_ms ();
        }
        for (std::map<owner_id_t, owner_state_t>::iterator it = node->owners.begin ();
             it != node->owners.end (); ++it) {
            for (int d = 0; d < 2; ++d) {
                if (it->second.domains[d].claimed)
                    it->second.domains[d].revoked = true;
            }
        }
        node->operations.clear ();
        node->cv.notify_all ();
    }

    wire_stop (node);
    unregister_node (node);
    delete node;
    *mesh_node_p_ = NULL;
    return ZLINK_CLOSE_OK;
}

//  --- peers -------------------------------------------------------------------

zlink_connect_result_t zlink_mesh_node_connect_peer (
  void *mesh_node_,
  const zlink_mesh_peer_connection_options_t *options_,
  uint64_t *connection_intent_id_out_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONNECT_INVALID_HANDLE;
    }
    if (check_versioned (options_) != 0)
        return ZLINK_CONNECT_INVALID_ARGUMENT;
    if (!options_->endpoint || options_->endpoint_size == 0
        || options_->endpoint_size > ZLINK_MESH_ENDPOINT_MAX
        || memchr (options_->endpoint, 0, options_->endpoint_size)) {
        errno = EINVAL;
        return ZLINK_CONNECT_INVALID_ARGUMENT;
    }
    if (options_->has_expected_rid
        && (options_->expected_rid.size == 0
            || options_->expected_rid.size > sizeof (options_->expected_rid.data))) {
        errno = EINVAL;
        return ZLINK_CONNECT_INVALID_ARGUMENT;
    }

    const std::string endpoint (options_->endpoint, options_->endpoint_size);

    std::unique_lock<std::mutex> lock (node->mutex);
    if (node->state == ZLINK_MESH_NODE_DRAINING || node->state == ZLINK_MESH_NODE_STOPPED) {
        errno = ESHUTDOWN;
        return ZLINK_CONNECT_BUSY;
    }

    //  A manual intent for an endpoint already observed merges sources.
    for (size_t i = 0; i < node->peers.size (); ++i) {
        peer_state_t &peer = node->peers[i];
        if (peer.endpoint == endpoint && peer.state != ZLINK_MESH_PEER_CLOSED) {
            if (peer.source == ZLINK_MESH_PEER_DISCOVERY)
                peer.source = ZLINK_MESH_PEER_MIXED;
            if (connection_intent_id_out_)
                *connection_intent_id_out_ = peer.intent_id;
            return ZLINK_CONNECT_OK;
        }
    }

    peer_state_t peer;
    peer.intent_id = node->next_intent_id++;
    peer.source = ZLINK_MESH_PEER_MANUAL;
    peer.state = ZLINK_MESH_PEER_CONNECTING;
    peer.endpoint = endpoint;
    peer.has_expected_rid = options_->has_expected_rid != 0;
    if (peer.has_expected_rid)
        peer.expected_rid = rid_bytes (options_->expected_rid);
    peer.last_changed_ms = now_ms ();
    node->peers.push_back (peer);
    const uint64_t intent_id = peer.intent_id;
    if (connection_intent_id_out_)
        *connection_intent_id_out_ = intent_id;
    recompute_readiness_locked (node);
    lock.unlock ();

    if (wire_connect_endpoint (node, endpoint) != 0) {
        const int reason = errno;
        std::lock_guard<std::mutex> relock (node->mutex);
        for (size_t i = 0; i < node->peers.size (); ++i) {
            if (node->peers[i].intent_id == intent_id) {
                node->peers[i].state = ZLINK_MESH_PEER_ERROR;
                node->peers[i].last_error = reason;
                node->peers[i].last_changed_ms = now_ms ();
                recompute_readiness_locked (node);
                break;
            }
        }
    }

    zlink_mesh_monitor_event_t event;
    memset (&event, 0, sizeof (event));
    event.kind = ZLINK_MESH_MONITOR_PEER_CONNECTING;
    emit_monitor_event (node, event);
    return ZLINK_CONNECT_OK;
}

zlink_connect_result_t zlink_mesh_node_remove_peer_connection (void *mesh_node_,
                                                               uint64_t connection_intent_id_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONNECT_INVALID_HANDLE;
    }
    std::lock_guard<std::mutex> lock (node->mutex);
    for (size_t i = 0; i < node->peers.size (); ++i) {
        peer_state_t &peer = node->peers[i];
        if (peer.intent_id != connection_intent_id_ || peer.state == ZLINK_MESH_PEER_CLOSED)
            continue;
        if (peer.source == ZLINK_MESH_PEER_MIXED) {
            //  Removing one source keeps the connection under the other.
            peer.source = ZLINK_MESH_PEER_DISCOVERY;
            return ZLINK_CONNECT_OK;
        }
        peer.state = ZLINK_MESH_PEER_CLOSED;
        peer.last_changed_ms = now_ms ();
        recompute_readiness_locked (node);
        return ZLINK_CONNECT_OK;
    }
    errno = ENOENT;
    return ZLINK_CONNECT_NOT_FOUND;
}

zlink_connect_result_t zlink_mesh_node_disconnect_peer (void *mesh_node_,
                                                        const zlink_routing_id_t *peer_rid_,
                                                        uint64_t lifecycle_generation_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONNECT_INVALID_HANDLE;
    }
    if (!peer_rid_ || peer_rid_->size == 0) {
        errno = EINVAL;
        return ZLINK_CONNECT_INVALID_ARGUMENT;
    }
    const rid_bytes_t rid = rid_bytes (*peer_rid_);
    bool closed = false;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        for (size_t i = 0; i < node->peers.size () && !closed; ++i) {
            peer_state_t &peer = node->peers[i];
            if (peer.state != ZLINK_MESH_PEER_ADMITTED && peer.state != ZLINK_MESH_PEER_DRAINING)
                continue;
            if (peer.rid != rid)
                continue;
            if (peer.lifecycle_generation != lifecycle_generation_) {
                errno = ESTALE;
                return ZLINK_CONNECT_CONFLICT;
            }
            peer.state = ZLINK_MESH_PEER_CLOSED;
            peer.last_changed_ms = now_ms ();
            recompute_readiness_locked (node);
            closed = true;
        }
    }
    if (closed) {
        zlink_mesh_monitor_event_t event;
        memset (&event, 0, sizeof (event));
        event.kind = ZLINK_MESH_MONITOR_PEER_CLOSED;
        event.peer_rid = *peer_rid_;
        event.peer_lifecycle_generation = lifecycle_generation_;
        emit_monitor_event (node, event);
        return ZLINK_CONNECT_OK;
    }
    errno = ENOENT;
    return ZLINK_CONNECT_NOT_FOUND;
}

//  --- options -----------------------------------------------------------------

zlink_config_result_t zlink_set_mesh_node_option (void *mesh_node_,
                                                  zlink_mesh_node_option_t option_,
                                                  const void *optval_,
                                                  size_t optvallen_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    std::lock_guard<std::mutex> lock (node->mutex);
    if (node->state != ZLINK_MESH_NODE_CREATED) {
        errno = EBUSY;
        return ZLINK_CONFIG_INVALID_STATE;
    }
    switch (option_) {
        case ZLINK_MESH_NODE_OPT_ROUTER_HWM_PROFILE: {
            if (!optval_ || optvallen_ != sizeof (int)) {
                errno = EMSGSIZE;
                return ZLINK_CONFIG_INVALID_ARGUMENT;
            }
            const int value = *static_cast<const int *> (optval_);
            if (value < ZLINK_AUTO_HWM_PROFILE_COMPACT || value > ZLINK_AUTO_HWM_PROFILE_THROUGHPUT) {
                errno = EINVAL;
                return ZLINK_CONFIG_INVALID_ARGUMENT;
            }
            node->router_hwm_profile = value;
            return ZLINK_CONFIG_OK;
        }
        case ZLINK_MESH_NODE_OPT_ROUTER_HWM: {
            if (!optval_ || optvallen_ != sizeof (int)) {
                errno = EMSGSIZE;
                return ZLINK_CONFIG_INVALID_ARGUMENT;
            }
            const int value = *static_cast<const int *> (optval_);
            if (value < 0) {
                errno = EINVAL;
                return ZLINK_CONFIG_INVALID_ARGUMENT;
            }
            node->router_hwm_override = value;
            return ZLINK_CONFIG_OK;
        }
        case ZLINK_MESH_NODE_OPT_MAILBOX_MESSAGE_BUDGET:
        case ZLINK_MESH_NODE_OPT_MAILBOX_BYTE_BUDGET: {
            if (!optval_ || optvallen_ != sizeof (uint64_t)) {
                errno = EMSGSIZE;
                return ZLINK_CONFIG_INVALID_ARGUMENT;
            }
            const uint64_t value = *static_cast<const uint64_t *> (optval_);
            if (option_ == ZLINK_MESH_NODE_OPT_MAILBOX_MESSAGE_BUDGET)
                node->mailbox_message_budget = value;
            else
                node->mailbox_byte_budget = value;
            return ZLINK_CONFIG_OK;
        }
        default:
            errno = ENOTSUP;
            return ZLINK_CONFIG_NOT_SUPPORTED;
    }
}

zlink_config_result_t zlink_get_mesh_node_option (void *mesh_node_,
                                                  zlink_mesh_node_option_t option_,
                                                  void *optval_,
                                                  size_t *optvallen_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (!optval_ || !optvallen_) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    std::lock_guard<std::mutex> lock (node->mutex);
    switch (option_) {
        case ZLINK_MESH_NODE_OPT_ROUTER_HWM_PROFILE:
        case ZLINK_MESH_NODE_OPT_ROUTER_HWM: {
            if (*optvallen_ < sizeof (int)) {
                *optvallen_ = sizeof (int);
                errno = ENOBUFS;
                return ZLINK_CONFIG_BUFFER_TOO_SMALL;
            }
            *static_cast<int *> (optval_) = option_ == ZLINK_MESH_NODE_OPT_ROUTER_HWM_PROFILE
                                              ? node->router_hwm_profile
                                              : node->router_hwm_override;
            *optvallen_ = sizeof (int);
            return ZLINK_CONFIG_OK;
        }
        case ZLINK_MESH_NODE_OPT_MAILBOX_MESSAGE_BUDGET:
        case ZLINK_MESH_NODE_OPT_MAILBOX_BYTE_BUDGET: {
            if (*optvallen_ < sizeof (uint64_t)) {
                *optvallen_ = sizeof (uint64_t);
                errno = ENOBUFS;
                return ZLINK_CONFIG_BUFFER_TOO_SMALL;
            }
            *static_cast<uint64_t *> (optval_) =
              option_ == ZLINK_MESH_NODE_OPT_MAILBOX_MESSAGE_BUDGET ? node->mailbox_message_budget
                                                                    : node->mailbox_byte_budget;
            *optvallen_ = sizeof (uint64_t);
            return ZLINK_CONFIG_OK;
        }
        default:
            errno = ENOTSUP;
            return ZLINK_CONFIG_NOT_SUPPORTED;
    }
}

//  --- status / query -----------------------------------------------------------

zlink_config_result_t zlink_mesh_node_status (void *mesh_node_, zlink_mesh_node_status_t *status_out_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (check_versioned (status_out_) != 0)
        return ZLINK_CONFIG_INVALID_ARGUMENT;

    std::lock_guard<std::mutex> lock (node->mutex);
    zlink_mesh_node_status_t out;
    init_versioned (&out);
    out.state = node->state;
    out.routing_id = rid_value (node->routing_id);
    snprintf (out.mesh_name, sizeof (out.mesh_name), "%s", node->mesh_name.c_str ());
    snprintf (out.local_endpoint, sizeof (out.local_endpoint), "%s", node->bind_endpoint.c_str ());
    out.lifecycle_generation = node->lifecycle_generation;
    out.descriptor_revision = node->descriptor_revision;
    out.channel_count = static_cast<uint32_t> (node->channels.size ());
    uint32_t configured = 0, admitted = 0, draining = 0;
    for (size_t i = 0; i < node->peers.size (); ++i) {
        if (node->peers[i].state == ZLINK_MESH_PEER_CLOSED)
            continue;
        ++configured;
        if (node->peers[i].state == ZLINK_MESH_PEER_ADMITTED)
            ++admitted;
        if (node->peers[i].state == ZLINK_MESH_PEER_DRAINING)
            ++draining;
    }
    out.configured_peer_count = configured;
    out.admitted_peer_count = admitted;
    out.draining_peer_count = draining;
    for (std::map<owner_id_t, owner_state_t>::const_iterator it = node->owners.begin ();
         it != node->owners.end (); ++it) {
        out.pending_application_messages += it->second.domains[0].pending_messages;
        out.pending_infrastructure_messages += it->second.domains[1].pending_messages;
        out.pending_bytes += it->second.domains[0].pending_bytes + it->second.domains[1].pending_bytes;
    }
    if (node->monitor) {
        out.multicast_submitted = node->monitor->counters.multicast_messages;
        out.multicast_dropped_targets = node->monitor->counters.multicast_dropped_targets;
    }
    out.last_error = node->last_error;
    out.last_changed_ms = node->last_changed_ms;
    *status_out_ = out;
    return ZLINK_CONFIG_OK;
}

zlink_config_result_t
zlink_mesh_node_peers (void *mesh_node_, zlink_mesh_peer_entry_t *entries_, size_t *count_inout_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (!count_inout_) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }

    std::lock_guard<std::mutex> lock (node->mutex);
    std::vector<const peer_state_t *> live;
    for (size_t i = 0; i < node->peers.size (); ++i) {
        if (node->peers[i].state != ZLINK_MESH_PEER_CLOSED)
            live.push_back (&node->peers[i]);
    }
    if (!entries_) {
        *count_inout_ = live.size ();
        return ZLINK_CONFIG_OK;
    }
    if (*count_inout_ < live.size ()) {
        *count_inout_ = live.size ();
        errno = ENOBUFS;
        return ZLINK_CONFIG_BUFFER_TOO_SMALL;
    }
    for (size_t i = 0; i < live.size (); ++i) {
        if (check_versioned (&entries_[i]) != 0)
            return ZLINK_CONFIG_INVALID_ARGUMENT;
        zlink_mesh_peer_entry_t out;
        init_versioned (&out);
        out.connection_intent_id = live[i]->intent_id;
        out.source = live[i]->source;
        out.state = live[i]->state;
        out.routing_id = rid_value (live[i]->rid);
        out.lifecycle_generation = live[i]->lifecycle_generation;
        out.descriptor_revision = live[i]->descriptor_revision;
        snprintf (out.endpoint, sizeof (out.endpoint), "%s", live[i]->endpoint.c_str ());
        out.channel_count = static_cast<uint32_t> (live[i]->channels.size ());
        out.last_error = live[i]->last_error;
        out.last_changed_ms = live[i]->last_changed_ms;
        entries_[i] = out;
    }
    *count_inout_ = live.size ();
    return ZLINK_CONFIG_OK;
}

zlink_config_result_t
zlink_mesh_node_peer_channels (void *mesh_node_,
                               const zlink_routing_id_t *peer_rid_,
                               uint64_t lifecycle_generation_,
                               char (*channel_names_out_)[ZLINK_CHANNEL_NAME_MAX + 1],
                               uint32_t *weights_out_,
                               size_t *count_inout_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (!peer_rid_ || !count_inout_) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    const rid_bytes_t rid = rid_bytes (*peer_rid_);
    std::lock_guard<std::mutex> lock (node->mutex);
    const peer_state_t *peer = NULL;
    for (size_t i = 0; i < node->peers.size (); ++i) {
        if (node->peers[i].rid == rid && node->peers[i].state != ZLINK_MESH_PEER_CLOSED
            && node->peers[i].lifecycle_generation == lifecycle_generation_) {
            peer = &node->peers[i];
            break;
        }
    }
    if (!peer) {
        errno = ENOENT;
        return ZLINK_CONFIG_NOT_FOUND;
    }
    if (!channel_names_out_ || !weights_out_) {
        *count_inout_ = peer->channels.size ();
        return ZLINK_CONFIG_OK;
    }
    if (*count_inout_ < peer->channels.size ()) {
        *count_inout_ = peer->channels.size ();
        errno = ENOBUFS;
        return ZLINK_CONFIG_BUFFER_TOO_SMALL;
    }
    size_t i = 0;
    for (std::map<std::string, uint32_t>::const_iterator it = peer->channels.begin ();
         it != peer->channels.end (); ++it, ++i) {
        snprintf (channel_names_out_[i], ZLINK_CHANNEL_NAME_MAX + 1, "%s", it->first.c_str ());
        weights_out_[i] = it->second;
    }
    *count_inout_ = peer->channels.size ();
    return ZLINK_CONFIG_OK;
}

//  --- Spot lifecycle -------------------------------------------------------------

void *zlink_spot_new (void *mesh_node_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return NULL;
    }
    std::lock_guard<std::mutex> lock (node->mutex);
    if (node->state == ZLINK_MESH_NODE_CREATED || node->state == ZLINK_MESH_NODE_DRAINING
        || node->state == ZLINK_MESH_NODE_STOPPED) {
        errno = node->state == ZLINK_MESH_NODE_CREATED ? EINVAL : ESHUTDOWN;
        return NULL;
    }
    const std::string key (node->routing_id.begin (), node->routing_id.end ());
    std::map<std::string, spot_state_t>::iterator it = node->spots.find (key);
    if (it == node->spots.end ()) {
        errno = EINVAL;
        return NULL;
    }
    return new_facade (node, it->second);
}

zlink_config_result_t zlink_mesh_node_entry_spot (void *mesh_node_, void **spot_out_)
{
    if (!spot_out_) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    void *facade = zlink_spot_new (mesh_node_);
    if (!facade)
        return errno == EFAULT ? ZLINK_CONFIG_INVALID_HANDLE
               : errno == ESHUTDOWN ? ZLINK_CONFIG_INVALID_STATE
                                    : ZLINK_CONFIG_INVALID_STATE;
    *spot_out_ = facade;
    return ZLINK_CONFIG_OK;
}

zlink_config_result_t
zlink_mesh_node_spot_lookup (void *mesh_node_, const zlink_routing_id_t *spot_rid_, void **spot_out_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node || !spot_out_) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (!spot_rid_ || spot_rid_->size == 0) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock (node->mutex);
    if (node->state == ZLINK_MESH_NODE_DRAINING || node->state == ZLINK_MESH_NODE_STOPPED) {
        errno = ESHUTDOWN;
        return ZLINK_CONFIG_INVALID_STATE;
    }
    const rid_bytes_t rid = rid_bytes (*spot_rid_);
    const std::string key (rid.begin (), rid.end ());
    std::map<std::string, spot_state_t>::iterator it = node->spots.find (key);
    if (it == node->spots.end ()) {
        errno = ENOENT;
        return ZLINK_CONFIG_NOT_FOUND;
    }
    spot_facade_t *facade = new_facade (node, it->second);
    if (!facade)
        return ZLINK_CONFIG_INTERNAL_ERROR;
    *spot_out_ = facade;
    return ZLINK_CONFIG_OK;
}

zlink_config_result_t zlink_mesh_node_spot_get_or_new (void *mesh_node_,
                                                       const zlink_routing_id_t *spot_rid_,
                                                       void **spot_out_,
                                                       uint32_t *created_out_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node || !spot_out_) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (!spot_rid_ || spot_rid_->size == 0) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock (node->mutex);
    if (node->state == ZLINK_MESH_NODE_CREATED) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_STATE;
    }
    if (node->state == ZLINK_MESH_NODE_DRAINING || node->state == ZLINK_MESH_NODE_STOPPED) {
        errno = ESHUTDOWN;
        return ZLINK_CONFIG_INVALID_STATE;
    }
    const rid_bytes_t rid = rid_bytes (*spot_rid_);
    const std::string key (rid.begin (), rid.end ());
    std::map<std::string, spot_state_t>::iterator it = node->spots.find (key);
    bool created = false;
    if (it == node->spots.end ()) {
        create_spot_locked (node, rid, ZLINK_SPOT_KIND_USER);
        it = node->spots.find (key);
        created = true;
    }
    spot_facade_t *facade = new_facade (node, it->second);
    if (!facade)
        return ZLINK_CONFIG_INTERNAL_ERROR;
    *spot_out_ = facade;
    if (created_out_)
        *created_out_ = created ? 1 : 0;
    return ZLINK_CONFIG_OK;
}

zlink_close_result_t zlink_spot_destroy (void **spot_p_)
{
    if (!spot_p_) {
        errno = EFAULT;
        return ZLINK_CLOSE_INVALID_HANDLE;
    }
    spot_facade_t *facade = as_spot_facade (*spot_p_);
    if (!facade) {
        errno = EFAULT;
        return ZLINK_CLOSE_INVALID_HANDLE;
    }
    mesh_node_t *node = facade->node;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        const std::string key (facade->spot_rid.begin (), facade->spot_rid.end ());
        std::map<std::string, spot_state_t>::iterator it = node->spots.find (key);
        if (it != node->spots.end () && it->second.generation == facade->generation
            && it->second.facade_count > 0)
            it->second.facade_count -= 1;
    }
    track_facade (facade, false);
    delete facade;
    *spot_p_ = NULL;
    return ZLINK_CLOSE_OK;
}

zlink_config_result_t zlink_spot_status (void *spot_, zlink_spot_status_t *status_out_)
{
    spot_facade_t *facade = as_spot_facade (spot_);
    if (!facade) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (check_versioned (status_out_) != 0)
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    mesh_node_t *node = facade->node;
    std::lock_guard<std::mutex> lock (node->mutex);
    const std::string key (facade->spot_rid.begin (), facade->spot_rid.end ());
    std::map<std::string, spot_state_t>::iterator it = node->spots.find (key);
    if (it == node->spots.end () || it->second.generation != facade->generation) {
        errno = ESTALE;
        return ZLINK_CONFIG_INVALID_STATE;
    }
    zlink_spot_status_t out;
    init_versioned (&out);
    out.spot_rid = rid_value (it->second.rid);
    out.spot_kind = it->second.kind;
    out.lifecycle_generation = it->second.generation;
    const owner_id_t owner = spot_owner (it->second.rid, it->second.generation);
    std::map<owner_id_t, owner_state_t>::const_iterator owner_it = node->owners.find (owner);
    if (owner_it != node->owners.end ()) {
        out.pending_application_messages = owner_it->second.domains[0].pending_messages;
        out.pending_infrastructure_messages = owner_it->second.domains[1].pending_messages;
        out.pending_bytes =
          owner_it->second.domains[0].pending_bytes + owner_it->second.domains[1].pending_bytes;
    }
    out.active_actor_count = it->second.active_actor_count;
    out.draining = it->second.draining ? 1 : 0;
    out.last_error = it->second.last_error;
    out.last_changed_ms = it->second.last_changed_ms;
    *status_out_ = out;
    return ZLINK_CONFIG_OK;
}

//  --- publisher -------------------------------------------------------------------

void *zlink_mesh_node_publisher_new (void *mesh_node_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return NULL;
    }
    std::lock_guard<std::mutex> lock (node->mutex);
    if (node->state == ZLINK_MESH_NODE_DRAINING || node->state == ZLINK_MESH_NODE_STOPPED) {
        errno = ESHUTDOWN;
        return NULL;
    }
    publisher_t *pub = new (std::nothrow) publisher_t ();
    if (!pub) {
        errno = ENOMEM;
        return NULL;
    }
    pub->node = node;
    node->publisher_count += 1;
    track_publisher (pub, true);
    return pub;
}

zlink_close_result_t zlink_mesh_node_publisher_destroy (void **publisher_p_)
{
    if (!publisher_p_) {
        errno = EFAULT;
        return ZLINK_CLOSE_INVALID_HANDLE;
    }
    publisher_t *pub = as_publisher (*publisher_p_);
    if (!pub) {
        errno = EFAULT;
        return ZLINK_CLOSE_INVALID_HANDLE;
    }
    {
        std::lock_guard<std::mutex> lock (pub->node->mutex);
        if (pub->node->publisher_count > 0)
            pub->node->publisher_count -= 1;
    }
    track_publisher (pub, false);
    delete pub;
    *publisher_p_ = NULL;
    return ZLINK_CLOSE_OK;
}

static zlink_config_result_t
publish_option_set (int *slot_, zlink_mesh_publish_option_t option_, const void *optval_, size_t len_)
{
    if (option_ != ZLINK_MESH_PUBLISH_OPT_NODROP) {
        errno = ENOTSUP;
        return ZLINK_CONFIG_NOT_SUPPORTED;
    }
    if (!optval_ || len_ != sizeof (int)) {
        errno = EMSGSIZE;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    const int value = *static_cast<const int *> (optval_);
    if (value != 0 && value != 1) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    *slot_ = value;
    return ZLINK_CONFIG_OK;
}

static zlink_config_result_t
publish_option_get (int value_, zlink_mesh_publish_option_t option_, void *optval_, size_t *len_)
{
    if (option_ != ZLINK_MESH_PUBLISH_OPT_NODROP) {
        errno = ENOTSUP;
        return ZLINK_CONFIG_NOT_SUPPORTED;
    }
    if (!optval_ || !len_) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (*len_ < sizeof (int)) {
        *len_ = sizeof (int);
        errno = ENOBUFS;
        return ZLINK_CONFIG_BUFFER_TOO_SMALL;
    }
    *static_cast<int *> (optval_) = value_;
    *len_ = sizeof (int);
    return ZLINK_CONFIG_OK;
}

zlink_config_result_t zlink_mesh_node_publisher_set_option (void *publisher_,
                                                            zlink_mesh_publish_option_t option_,
                                                            const void *optval_,
                                                            size_t optvallen_)
{
    publisher_t *pub = as_publisher (publisher_);
    if (!pub) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    std::lock_guard<std::mutex> lock (pub->node->mutex);
    return publish_option_set (&pub->nodrop, option_, optval_, optvallen_);
}

zlink_config_result_t zlink_mesh_node_publisher_get_option (void *publisher_,
                                                            zlink_mesh_publish_option_t option_,
                                                            void *optval_,
                                                            size_t *optvallen_)
{
    publisher_t *pub = as_publisher (publisher_);
    if (!pub) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    std::lock_guard<std::mutex> lock (pub->node->mutex);
    return publish_option_get (pub->nodrop, option_, optval_, optvallen_);
}

zlink_config_result_t zlink_spot_set_publish_option (void *spot_,
                                                     zlink_mesh_publish_option_t option_,
                                                     const void *optval_,
                                                     size_t optvallen_)
{
    spot_facade_t *facade = as_spot_facade (spot_);
    if (!facade) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    std::lock_guard<std::mutex> lock (facade->node->mutex);
    const std::string key (facade->spot_rid.begin (), facade->spot_rid.end ());
    std::map<std::string, spot_state_t>::iterator it = facade->node->spots.find (key);
    if (it == facade->node->spots.end () || it->second.generation != facade->generation) {
        errno = ESTALE;
        return ZLINK_CONFIG_INVALID_STATE;
    }
    return publish_option_set (&it->second.publish_nodrop, option_, optval_, optvallen_);
}

zlink_config_result_t zlink_spot_get_publish_option (void *spot_,
                                                     zlink_mesh_publish_option_t option_,
                                                     void *optval_,
                                                     size_t *optvallen_)
{
    spot_facade_t *facade = as_spot_facade (spot_);
    if (!facade) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    std::lock_guard<std::mutex> lock (facade->node->mutex);
    const std::string key (facade->spot_rid.begin (), facade->spot_rid.end ());
    std::map<std::string, spot_state_t>::iterator it = facade->node->spots.find (key);
    if (it == facade->node->spots.end () || it->second.generation != facade->generation) {
        errno = ESTALE;
        return ZLINK_CONFIG_INVALID_STATE;
    }
    return publish_option_get (it->second.publish_nodrop, option_, optval_, optvallen_);
}
