/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/mesh/mesh_c_internal.hpp"
#include "api/mesh/mesh_stream_session_internal.hpp"
#include "services/mesh/mesh_wire.hpp"

#include "utils/err.hpp"
#include "utils/macros.hpp"

#include <string.h>

using namespace zlink::mesh;

//  Actor service C surface. Actor identity, mailbox ownership and Spot
//  membership are process-local state on the owner MeshNode; cross-node
//  routes and the transfer data plane engage once peers are admitted.

namespace
{
int check_actor_ref (const zlink_actor_ref_t *actor_)
{
    if (!actor_) {
        errno = EINVAL;
        return -1;
    }
    const size_t len = strnlen (actor_->actor_id, sizeof (actor_->actor_id));
    if (len == 0 || len > ZLINK_ACTOR_ID_MAX || actor_->generation == 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

//  Resolves a live local actor by ref. Caller holds the node mutex.
actor_state_t *find_actor_locked (mesh_node_t *node_, const zlink_actor_ref_t *actor_, int *err_)
{
    const std::string id (actor_->actor_id);
    std::map<std::string, actor_state_t>::iterator it = node_->actors.find (id);
    if (it == node_->actors.end ()) {
        *err_ = ENOENT;
        return NULL;
    }
    if (it->second.generation != actor_->generation) {
        *err_ = ESTALE;
        return NULL;
    }
    if (!rid_equal (actor_->node_rid, rid_value (node_->routing_id))) {
        *err_ = ENOTCONN;
        return NULL;
    }
    //  A destroy-draining actor admits no new work. A transfer fence keeps
    //  the actor resolvable so admission can report its contract EAGAIN.
    if (it->second.draining) {
        std::map<owner_id_t, owner_state_t>::const_iterator owner_it =
          node_->owners.find (actor_owner (id, it->second.generation));
        const bool fenced = owner_it != node_->owners.end ()
                            && owner_it->second.fenced_transfer_serial != 0;
        if (!fenced) {
            *err_ = ESHUTDOWN;
            return NULL;
        }
    }
    return &it->second;
}

void fill_location (const mesh_node_t *node_,
                    const actor_state_t &actor_,
                    zlink_actor_location_t *out_)
{
    init_versioned (out_);
    out_->actor.node_rid = rid_value (node_->routing_id);
    snprintf (out_->actor.actor_id, sizeof (out_->actor.actor_id), "%s", actor_.id.c_str ());
    out_->actor.generation = actor_.generation;
    out_->spot_rid = rid_value (actor_.spot_rid);
    out_->spot_generation = actor_.spot_generation;
    out_->membership_epoch = actor_.membership_epoch;
}

//  Builds the SPOT_CONTROL record for a lifecycle transition.
std::unique_ptr<queued_record_t> control_record (mesh_node_t *node_,
                                                 zlink_actor_lifecycle_kind_t kind_,
                                                 const actor_state_t &previous_,
                                                 const actor_state_t &current_,
                                                 int32_t result_code_)
{
    std::unique_ptr<queued_record_t> record (new (std::nothrow) queued_record_t ());
    if (!record.get ())
        return record;
    record->kind = ZLINK_MESH_RECORD_SPOT_CONTROL;
    record->source_node_rid = node_->routing_id;

    zlink_actor_control_record_t data;
    memset (&data, 0, sizeof (data));
    data.struct_size = sizeof (data);
    data.version = 1;
    data.kind = kind_;
    data.previous_actor.node_rid = rid_value (node_->routing_id);
    snprintf (data.previous_actor.actor_id, sizeof (data.previous_actor.actor_id), "%s",
              previous_.id.c_str ());
    data.previous_actor.generation = previous_.generation;
    data.current_actor.node_rid = rid_value (node_->routing_id);
    snprintf (data.current_actor.actor_id, sizeof (data.current_actor.actor_id), "%s",
              current_.id.c_str ());
    data.current_actor.generation = current_.generation;
    data.previous_spot_rid = rid_value (previous_.spot_rid);
    data.current_spot_rid = rid_value (current_.spot_rid);
    data.previous_spot_generation = previous_.spot_generation;
    data.current_spot_generation = current_.spot_generation;
    data.previous_membership_epoch = previous_.membership_epoch;
    data.current_membership_epoch = current_.membership_epoch;
    data.result_code = result_code_;

    record->kind_data.assign (reinterpret_cast<unsigned char *> (&data),
                              reinterpret_cast<unsigned char *> (&data) + sizeof (data));
    return record;
}
}

namespace zlink
{
namespace mesh
{
int actor_lookup_local (mesh_node_t *node_,
                        const std::string &actor_id_,
                        zlink_actor_ref_t *ref_out_,
                        rid_bytes_t *spot_rid_out_,
                        uint64_t *spot_generation_out_,
                        uint64_t *membership_epoch_out_)
{
    std::lock_guard<std::mutex> lock (node_->mutex);
    std::map<std::string, actor_state_t>::iterator it = node_->actors.find (actor_id_);
    if (it == node_->actors.end ()) {
        errno = ENOENT;
        return -1;
    }
    memset (ref_out_, 0, sizeof (*ref_out_));
    ref_out_->node_rid = rid_value (node_->routing_id);
    snprintf (ref_out_->actor_id, sizeof (ref_out_->actor_id), "%s", it->second.id.c_str ());
    ref_out_->generation = it->second.generation;
    *spot_rid_out_ = it->second.spot_rid;
    *spot_generation_out_ = it->second.spot_generation;
    *membership_epoch_out_ = it->second.membership_epoch;
    return 0;
}

int actor_destroy_local (mesh_node_t *node_, const zlink_actor_ref_t *actor_)
{
    actor_state_t before;
    {
        std::unique_lock<std::mutex> lock (node_->mutex);
        const std::string id (actor_->actor_id);
        std::map<std::string, actor_state_t>::iterator it = node_->actors.find (id);
        if (it == node_->actors.end ()) {
            errno = ENOENT;
            return -1;
        }
        if (it->second.generation != actor_->generation) {
            errno = ESTALE;
            return -1;
        }
        before = it->second;
        const std::string spot_key (it->second.spot_rid.begin (), it->second.spot_rid.end ());
        std::map<std::string, spot_state_t>::iterator spot_it = node_->spots.find (spot_key);
        if (it->second.spot_node_rid.empty () && spot_it != node_->spots.end ()
            && spot_it->second.active_actor_count > 0) {
            spot_it->second.active_actor_count -= 1;
            maybe_end_spot_locked (node_, spot_key);
        }
        node_->owners.erase (actor_owner (id, it->second.generation));
        node_->actors.erase (it);
    }

    if (before.spot_node_rid.empty ()) {
        std::unique_ptr<queued_record_t> destroyed (new (std::nothrow) queued_record_t ());
        if (destroyed.get ()) {
            destroyed->kind = ZLINK_MESH_RECORD_SPOT_CONTROL;
            destroyed->source_node_rid = node_->routing_id;
            zlink_actor_control_record_t data;
            memset (&data, 0, sizeof (data));
            data.struct_size = sizeof (data);
            data.version = 1;
            data.kind = ZLINK_ACTOR_LIFECYCLE_DESTROYED;
            data.previous_actor = *actor_;
            data.current_actor = *actor_;
            data.previous_spot_rid = rid_value (before.spot_rid);
            data.current_spot_rid = rid_value (before.spot_rid);
            data.previous_spot_generation = before.spot_generation;
            data.current_spot_generation = before.spot_generation;
            data.previous_membership_epoch = before.membership_epoch;
            data.current_membership_epoch = before.membership_epoch;
            data.result_code = ZLINK_REQUEST_OK;
            destroyed->kind_data.assign (reinterpret_cast<unsigned char *> (&data),
                                         reinterpret_cast<unsigned char *> (&data)
                                           + sizeof (data));
            const owner_id_t spot_owner_id = spot_owner (before.spot_rid, before.spot_generation);
            (void) admit_record (node_, spot_owner_id, domain_infrastructure, destroyed, false, 0);
        }
    } else {
        //  The joined Spot lives on a peer: it observes the departure.
        wire_notify_actor_left (node_, before.spot_node_rid, *actor_, before.spot_rid,
                                before.spot_generation, before.membership_epoch,
                                before.membership_epoch);
    }
    return 0;
}

int actor_admit_remote_join (mesh_node_t *node_,
                             const rid_bytes_t &origin_rid_,
                             uint64_t origin_generation_,
                             uint64_t origin_correlation_,
                             const zlink_actor_ref_t &actor_,
                             bool entry_,
                             const rid_bytes_t &spot_rid_,
                             uint64_t spot_generation_,
                             std::vector<zlink_msg_t> *creation_parts_)
{
    rid_bytes_t target_spot = spot_rid_;
    uint64_t target_generation = spot_generation_;
    owner_id_t target_owner;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        if (entry_) {
            const std::string entry_key (node_->routing_id.begin (), node_->routing_id.end ());
            std::map<std::string, spot_state_t>::iterator it = node_->spots.find (entry_key);
            if (it == node_->spots.end () || it->second.draining) {
                errno = ESTALE;
                return -1;
            }
            target_spot = it->second.rid;
            target_generation = it->second.generation;
        } else {
            const std::string key (target_spot.begin (), target_spot.end ());
            std::map<std::string, spot_state_t>::iterator it = node_->spots.find (key);
            if (it == node_->spots.end () || it->second.generation != target_generation
                || it->second.draining) {
                errno = ESTALE;
                return -1;
            }
        }
        target_owner = spot_owner (target_spot, target_generation);
    }

    std::unique_ptr<queued_record_t> record (new (std::nothrow) queued_record_t ());
    if (!record.get ()) {
        errno = ENOMEM;
        return -1;
    }
    record->kind = ZLINK_MESH_RECORD_SPOT_CONTROL;
    record->source_node_rid = origin_rid_;
    zlink_actor_control_record_t data;
    memset (&data, 0, sizeof (data));
    data.struct_size = sizeof (data);
    data.version = 1;
    data.kind = ZLINK_ACTOR_LIFECYCLE_JOINED;
    data.previous_actor = actor_;
    data.current_actor = actor_;
    data.current_spot_rid = rid_value (target_spot);
    data.current_spot_generation = target_generation;
    data.result_code = ZLINK_REQUEST_OK;
    record->kind_data.assign (reinterpret_cast<unsigned char *> (&data),
                              reinterpret_cast<unsigned char *> (&data) + sizeof (data));
    record->operation_kind = ZLINK_MESH_OPERATION_ACTOR_JOIN;
    if (creation_parts_) {
        record->parts = std::move (*creation_parts_);
        creation_parts_->clear ();
        for (size_t i = 0; i < record->parts.size (); ++i)
            record->byte_size += zlink_msg_size (&record->parts[i]);
    }

    uint64_t reply_serial = 0;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        reply_serial = node_->next_reply_serial++;
        reply_route_t route;
        route.kind = reply_route_t::kind_actor_join;
        route.requester = node_owner ();
        route.requester_node_generation = node_->lifecycle_generation;
        route.operation_kind = ZLINK_MESH_OPERATION_ACTOR_JOIN;
        route.remote_origin = true;
        route.origin_rid = origin_rid_;
        route.origin_generation = origin_generation_;
        route.origin_correlation = origin_correlation_;
        route.join_actor = actor_;
        route.join_target_spot_rid = target_spot;
        route.join_target_spot_generation = target_generation;
        node_->reply_routes[reply_serial] = route;
        record->has_reply_token = true;
        seal_reply_token (node_, reply_serial, &record->reply_token);
    }

    if (admit_record (node_, target_owner, domain_application, record, false, 0) != 0) {
        const int reason = errno;
        std::lock_guard<std::mutex> lock (node_->mutex);
        node_->reply_routes.erase (reply_serial);
        errno = reason;
        return -1;
    }
    return 0;
}

void actor_apply_remote_join_reply (mesh_node_t *node_,
                                    const pending_operation_t &op_,
                                    uint32_t join_result_,
                                    const rid_bytes_t &spot_node_rid_,
                                    const rid_bytes_t &spot_rid_,
                                    uint64_t spot_generation_)
{
    zlink_actor_join_completion_t completion;
    memset (&completion, 0, sizeof (completion));
    completion.struct_size = sizeof (completion);
    completion.version = 1;
    completion.join_result = static_cast<zlink_actor_join_result_t> (join_result_);

    bool stale = false;
    rid_bytes_t left_notify_node;
    zlink_actor_ref_t left_actor;
    rid_bytes_t left_prev_spot;
    uint64_t left_prev_generation = 0;
    uint64_t left_prev_epoch = 0;
    uint64_t left_new_epoch = 0;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        const std::string id (op_.join_actor.actor_id);
        std::map<std::string, actor_state_t>::iterator it = node_->actors.find (id);
        if (it == node_->actors.end () || it->second.generation != op_.join_actor.generation) {
            stale = true;
        } else {
            actor_state_t &actor = it->second;
            if (join_result_ == ZLINK_ACTOR_JOIN_ACCEPTED) {
                //  The accepted reply is the single membership commit point.
                if (actor.spot_node_rid.empty ()) {
                    const std::string prev_key (actor.spot_rid.begin (), actor.spot_rid.end ());
                    std::map<std::string, spot_state_t>::iterator prev_it =
                      node_->spots.find (prev_key);
                    if (prev_it != node_->spots.end ()
                        && prev_it->second.active_actor_count > 0) {
                        prev_it->second.active_actor_count -= 1;
                        maybe_end_spot_locked (node_, prev_key);
                    }
                } else {
                    left_notify_node = actor.spot_node_rid;
                    left_actor = op_.join_actor;
                    left_prev_spot = actor.spot_rid;
                    left_prev_generation = actor.spot_generation;
                    left_prev_epoch = actor.membership_epoch;
                }
                actor.spot_rid = spot_rid_;
                actor.spot_generation = spot_generation_;
                actor.spot_node_rid = spot_node_rid_;
                actor.membership_epoch += 1;
                left_new_epoch = actor.membership_epoch;
            }
            completion.actor.node_rid = rid_value (node_->routing_id);
            snprintf (completion.actor.actor_id, sizeof (completion.actor.actor_id), "%s",
                      id.c_str ());
            completion.actor.generation = actor.generation;
            completion.location.struct_size = sizeof (completion.location);
            completion.location.version = 1;
            completion.location.actor = completion.actor;
            completion.location.spot_rid = rid_value (actor.spot_rid);
            completion.location.spot_generation = actor.spot_generation;
            completion.location.membership_epoch = actor.membership_epoch;
        }
    }
    if (stale) {
        complete_operation (node_, op_, ZLINK_REQUEST_CONFLICT, ESTALE, NULL, NULL);
        return;
    }
    if (!left_notify_node.empty ())
        wire_notify_actor_left (node_, left_notify_node, left_actor, left_prev_spot,
                                left_prev_generation, left_prev_epoch, left_new_epoch);

    std::vector<unsigned char> kind_data (reinterpret_cast<unsigned char *> (&completion),
                                          reinterpret_cast<unsigned char *> (&completion)
                                            + sizeof (completion));
    if (join_result_ == ZLINK_ACTOR_JOIN_ACCEPTED)
        complete_operation (node_, op_, ZLINK_REQUEST_OK, 0, &kind_data, NULL);
    else
        complete_operation (node_, op_, ZLINK_REQUEST_REJECTED, EACCES, &kind_data, NULL);
}
}
}

//  --- creation / lookup / destroy ------------------------------------------------

zlink_request_result_t zlink_mesh_node_actor_new (void *mesh_node_,
                                                  const char *actor_id_,
                                                  const zlink_msg_t *creation_parts_,
                                                  size_t creation_part_count_,
                                                  zlink_actor_ref_t *actor_out_,
                                                  zlink_send_flags_t flags_,
                                                  uint32_t timeout_ms_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    std::string id;
    if (check_name (actor_id_, ZLINK_ACTOR_ID_MAX, &id) != 0)
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    if ((creation_part_count_ > 0 && !creation_parts_)
        || (creation_part_count_ == 0 && creation_parts_)) {
        errno = EINVAL;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }

    std::unique_lock<std::mutex> lock (node->mutex);
    if (node->state == ZLINK_MESH_NODE_CREATED) {
        errno = EINVAL;
        return ZLINK_REQUEST_INVALID_STATE;
    }
    if (node->state == ZLINK_MESH_NODE_DRAINING || node->state == ZLINK_MESH_NODE_STOPPED) {
        errno = ESHUTDOWN;
        return ZLINK_REQUEST_INVALID_STATE;
    }
    if (node->actors.count (id)) {
        errno = EEXIST;
        return ZLINK_REQUEST_CONFLICT;
    }

    //  Entry Spot control mailbox admits the CREATED record; reserve first
    //  so a failed admission consumes no generation.
    const std::string entry_key (node->routing_id.begin (), node->routing_id.end ());
    std::map<std::string, spot_state_t>::iterator entry_it = node->spots.find (entry_key);
    if (entry_it == node->spots.end ()) {
        errno = EINVAL;
        return ZLINK_REQUEST_INVALID_STATE;
    }
    spot_state_t &entry_spot = entry_it->second;

    actor_state_t actor;
    actor.id = id;
    actor.generation = node->next_actor_generation; //  consumed only on success
    actor.membership_epoch = 1;
    actor.spot_rid = entry_spot.rid;
    actor.spot_generation = entry_spot.generation;

    std::unique_ptr<queued_record_t> record =
      control_record (node, ZLINK_ACTOR_LIFECYCLE_CREATED, actor, actor, ZLINK_REQUEST_OK);
    if (!record.get ()) {
        errno = ENOMEM;
        return ZLINK_REQUEST_INTERNAL_ERROR;
    }
    for (size_t i = 0; i < creation_part_count_; ++i) {
        record->parts.push_back (zlink_msg_t ());
        zlink_msg_init (&record->parts.back ());
        if (zlink_msg_copy (&record->parts.back (),
                            const_cast<zlink_msg_t *> (&creation_parts_[i]))
            != 0) {
            errno = EFAULT;
            return ZLINK_REQUEST_INTERNAL_ERROR;
        }
        record->byte_size += zlink_msg_size (&record->parts.back ());
    }

    const owner_id_t entry_owner = spot_owner (entry_spot.rid, entry_spot.generation);
    lock.unlock ();
    const bool blocking = (flags_ & ZLINK_SEND_FLAGS_DONTWAIT) == 0;
    if (admit_record (node, entry_owner, domain_application, record, blocking, timeout_ms_) != 0) {
        switch (errno) {
            case EAGAIN:
                return ZLINK_REQUEST_BACKPRESSURED;
            case ETIMEDOUT:
                return ZLINK_REQUEST_TIMED_OUT;
            case ESHUTDOWN:
                return ZLINK_REQUEST_TERMINATED;
            default:
                return ZLINK_REQUEST_INTERNAL_ERROR;
        }
    }

    lock.lock ();
    node->next_actor_generation += 1;
    node->actors[id] = actor;
    entry_spot.active_actor_count += 1;

    //  The actor owner mailboxes exist from creation.
    owner_state_t &owner = node->owners[actor_owner (id, actor.generation)];
    owner.id = actor_owner (id, actor.generation);
    owner.actor.node_rid = rid_value (node->routing_id);
    snprintf (owner.actor.actor_id, sizeof (owner.actor.actor_id), "%s", id.c_str ());
    owner.actor.generation = actor.generation;

    if (actor_out_) {
        memset (actor_out_, 0, sizeof (*actor_out_));
        actor_out_->node_rid = rid_value (node->routing_id);
        snprintf (actor_out_->actor_id, sizeof (actor_out_->actor_id), "%s", id.c_str ());
        actor_out_->generation = actor.generation;
    }
    return ZLINK_REQUEST_OK;
}

zlink_config_result_t zlink_mesh_node_actor_lookup (void *mesh_node_,
                                                    const char *actor_id_,
                                                    zlink_actor_location_t *location_out_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    std::string id;
    if (check_name (actor_id_, ZLINK_ACTOR_ID_MAX, &id) != 0)
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    if (check_versioned (location_out_) != 0)
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock (node->mutex);
    std::map<std::string, actor_state_t>::iterator it = node->actors.find (id);
    if (it == node->actors.end ()) {
        errno = ENOENT;
        return ZLINK_CONFIG_NOT_FOUND;
    }
    fill_location (node, it->second, location_out_);
    return ZLINK_CONFIG_OK;
}

zlink_submit_result_t zlink_mesh_node_actor_lookup_remote (void *mesh_node_,
                                                           const zlink_routing_id_t *target_node_rid_,
                                                           const char *actor_id_,
                                                           zlink_mesh_operation_id_t *operation_id_out_,
                                                           uint32_t timeout_ms_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (!target_node_rid_ || target_node_rid_->size == 0 || !operation_id_out_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    std::string id;
    if (check_name (actor_id_, ZLINK_ACTOR_ID_MAX, &id) != 0)
        return ZLINK_SUBMIT_INVALID_ARGUMENT;

    const rid_bytes_t target_node = rid_bytes (*target_node_rid_);
    {
        std::unique_lock<std::mutex> lock (node->mutex);
        if (node->state == ZLINK_MESH_NODE_DRAINING || node->state == ZLINK_MESH_NODE_STOPPED) {
            errno = ESHUTDOWN;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        bool admitted = false;
        for (size_t i = 0; i < node->peers.size (); ++i) {
            if (node->peers[i].state == ZLINK_MESH_PEER_ADMITTED
                && node->peers[i].rid == target_node) {
                admitted = true;
                break;
            }
        }
        if (!admitted) {
            errno = ENOTCONN;
            return ZLINK_SUBMIT_NOT_CONNECTED;
        }
    }
    const zlink_mesh_operation_id_t op_id =
      register_operation (node, ZLINK_MESH_OPERATION_ACTOR_LOOKUP, node_owner (), timeout_ms_);
    const zlink_submit_result_t rc = wire_submit_actor_lookup (node, target_node, op_id.low, id);
    if (rc != ZLINK_SUBMIT_OK) {
        std::lock_guard<std::mutex> lock (node->mutex);
        node->operations.erase (op_id.low);
        return rc;
    }
    if (schedule_operation_timeout (node, op_id.low, timeout_ms_) != 0)
        return ZLINK_SUBMIT_INTERNAL_ERROR;
    *operation_id_out_ = op_id;
    return ZLINK_SUBMIT_OK;
}

zlink_submit_result_t zlink_mesh_node_actor_destroy (void *mesh_node_,
                                                     const zlink_actor_ref_t *actor_,
                                                     zlink_mesh_operation_id_t *operation_id_out_,
                                                     uint32_t timeout_ms_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (check_actor_ref (actor_) != 0 || !operation_id_out_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    //  A foreign ActorRef destroys through the owning node's pipe.
    {
        std::unique_lock<std::mutex> lock (node->mutex);
        if (node->state == ZLINK_MESH_NODE_DRAINING || node->state == ZLINK_MESH_NODE_STOPPED) {
            errno = ESHUTDOWN;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        if (!rid_equal (actor_->node_rid, rid_value (node->routing_id))) {
            const rid_bytes_t target_node = rid_bytes (actor_->node_rid);
            bool admitted = false;
            for (size_t i = 0; i < node->peers.size (); ++i) {
                if (node->peers[i].state == ZLINK_MESH_PEER_ADMITTED
                    && node->peers[i].rid == target_node) {
                    admitted = true;
                    break;
                }
            }
            if (!admitted) {
                errno = ENOTCONN;
                return ZLINK_SUBMIT_NOT_CONNECTED;
            }
            lock.unlock ();
            const zlink_mesh_operation_id_t op_id = register_operation (
              node, ZLINK_MESH_OPERATION_ACTOR_DESTROY, node_owner (), timeout_ms_);
            const zlink_submit_result_t rc =
              wire_submit_actor_destroy (node, target_node, op_id.low, *actor_);
            if (rc != ZLINK_SUBMIT_OK) {
                std::lock_guard<std::mutex> relock (node->mutex);
                node->operations.erase (op_id.low);
                return rc;
            }
            if (schedule_operation_timeout (node, op_id.low, timeout_ms_) != 0)
                return ZLINK_SUBMIT_INTERNAL_ERROR;
            *operation_id_out_ = op_id;
            return ZLINK_SUBMIT_OK;
        }
    }

    pending_operation_t op;
    actor_state_t before;
    {
        std::unique_lock<std::mutex> lock (node->mutex);
        if (node->state == ZLINK_MESH_NODE_DRAINING || node->state == ZLINK_MESH_NODE_STOPPED) {
            errno = ESHUTDOWN;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        int err = 0;
        actor_state_t *actor = find_actor_locked (node, actor_, &err);
        if (!actor) {
            errno = err;
            return err == ESTALE ? ZLINK_SUBMIT_INVALID_STATE : ZLINK_SUBMIT_NOT_FOUND;
        }
        before = *actor;
        actor->draining = true;
    }

    const zlink_mesh_operation_id_t op_id =
      register_operation (node, ZLINK_MESH_OPERATION_ACTOR_DESTROY, node_owner (), timeout_ms_);
    *operation_id_out_ = op_id;

    //  Local destroy: with new admission closed by the draining mark, wait
    //  until the actor's active claims release and its outstanding request
    //  completions deliver, up to the deadline. Past the deadline the claims
    //  are revoked (release stays safe) and the removal proceeds.
    {
        std::unique_lock<std::mutex> lock (node->mutex);
        const std::string drain_id (actor_->actor_id);
        const owner_id_t drain_owner = actor_owner (drain_id, actor_->generation);
        const uint64_t deadline = timeout_ms_ > 0 ? now_ms () + timeout_ms_ : now_ms ();
        while (true) {
            std::map<std::string, actor_state_t>::iterator actor_it =
              node->actors.find (drain_id);
            if (actor_it == node->actors.end ()
                || actor_it->second.generation != actor_->generation)
                break;
            bool held = false;
            std::map<owner_id_t, owner_state_t>::iterator owner_it =
              node->owners.find (drain_owner);
            if (owner_it != node->owners.end ()
                && (owner_it->second.domains[0].claimed || owner_it->second.domains[1].claimed))
                held = true;
            bool completions_pending = false;
            for (std::unordered_map<uint64_t, pending_operation_t>::iterator op_it =
                   node->operations.begin ();
                 op_it != node->operations.end () && !completions_pending; ++op_it) {
                if (op_it->second.requester == drain_owner
                    && op_it->second.id.low != op_id.low)
                    completions_pending = true;
            }
            bool session_control_pending = false;
            if (!held && !completions_pending) {
                //  Bound session control drains without the node mutex; the
                //  session services use their own locks. Every iterator into
                //  node state is invalid after this window.
                lock.unlock ();
                session_control_pending = session_bindings_pending (node, *actor_);
                lock.lock ();
                owner_it = node->owners.find (drain_owner);
            }
            if (!held && !completions_pending && !session_control_pending)
                break;
            const uint64_t now = now_ms ();
            if (timeout_ms_ == 0 || now >= deadline) {
                if (owner_it != node->owners.end ()) {
                    for (int d = 0; d < 2; ++d) {
                        if (owner_it->second.domains[d].claimed)
                            owner_it->second.domains[d].revoked = true;
                    }
                }
                break;
            }
            node->cv.wait_for (lock, std::chrono::milliseconds (deadline - now));
        }
        lock.unlock ();
    }

    //  Emit DESTROYED to the owning Spot's control lane, drop the mailboxes,
    //  and complete the operation.
    {
        std::unique_lock<std::mutex> lock (node->mutex);
        const std::string id (actor_->actor_id);
        std::map<std::string, actor_state_t>::iterator it = node->actors.find (id);
        if (it != node->actors.end () && it->second.generation == actor_->generation) {
            const std::string spot_key (it->second.spot_rid.begin (), it->second.spot_rid.end ());
            std::map<std::string, spot_state_t>::iterator spot_it = node->spots.find (spot_key);
            //  Capture before maybe_end_spot_locked: ending the Spot erases
            //  the element and invalidates spot_it.
            const bool spot_present = spot_it != node->spots.end ();
            if (spot_present && spot_it->second.active_actor_count > 0) {
                spot_it->second.active_actor_count -= 1;
                maybe_end_spot_locked (node, spot_key);
            }
            node->owners.erase (actor_owner (id, it->second.generation));
            actor_state_t after = it->second;
            node->actors.erase (it);
            lock.unlock ();

            std::unique_ptr<queued_record_t> record = control_record (
              node, ZLINK_ACTOR_LIFECYCLE_DESTROYED, before, after, ZLINK_REQUEST_OK);
            if (record.get () && spot_present) {
                //  If the Spot ended above, the admission misses its owner
                //  and drops the record — an unreferenced Spot has no
                //  observer for DESTROYED anyway.
                const owner_id_t spot_owner_id =
                  spot_owner (before.spot_rid, before.spot_generation);
                (void) admit_record (node, spot_owner_id, domain_infrastructure, record, false, 0);
            }
        }
    }

    //  No session may keep addressing the destroyed generation.
    session_bindings_remove_actor (node, *actor_);

    pending_operation_t completed;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        std::unordered_map<uint64_t, pending_operation_t>::iterator op_it =
          node->operations.find (op_id.low);
        if (op_it == node->operations.end ())
            return ZLINK_SUBMIT_OK;
        completed = op_it->second;
        node->operations.erase (op_it);
    }
    complete_operation (node, completed, ZLINK_REQUEST_OK, 0, NULL, NULL);
    return ZLINK_SUBMIT_OK;
}

//  --- membership -------------------------------------------------------------------

zlink_submit_result_t zlink_mesh_node_actor_join_spot (void *mesh_node_,
                                                       const zlink_actor_ref_t *actor_,
                                                       const zlink_routing_id_t *target_node_rid_,
                                                       const zlink_routing_id_t *target_spot_rid_,
                                                       uint64_t target_spot_generation_,
                                                       const zlink_msg_t *creation_parts_,
                                                       size_t creation_part_count_,
                                                       zlink_mesh_operation_id_t *operation_id_out_,
                                                       uint32_t timeout_ms_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (check_actor_ref (actor_) != 0 || !operation_id_out_ || !target_node_rid_
        || target_node_rid_->size == 0 || !target_spot_rid_ || target_spot_rid_->size == 0) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (target_spot_generation_ == 0) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if ((creation_part_count_ > 0 && !creation_parts_)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    const rid_bytes_t target_node = rid_bytes (*target_node_rid_);
    const rid_bytes_t target_spot = rid_bytes (*target_spot_rid_);

    owner_id_t target_owner;
    actor_state_t actor_snapshot;
    {
        std::unique_lock<std::mutex> lock (node->mutex);
        if (node->state == ZLINK_MESH_NODE_DRAINING || node->state == ZLINK_MESH_NODE_STOPPED) {
            errno = ESHUTDOWN;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        int err = 0;
        actor_state_t *actor = find_actor_locked (node, actor_, &err);
        if (!actor) {
            errno = err;
            return err == ESTALE ? ZLINK_SUBMIT_INVALID_STATE : ZLINK_SUBMIT_NOT_FOUND;
        }
        actor_snapshot = *actor;
        if (target_node != node->routing_id) {
            bool admitted = false;
            for (size_t i = 0; i < node->peers.size (); ++i) {
                if (node->peers[i].state == ZLINK_MESH_PEER_ADMITTED
                    && node->peers[i].rid == target_node) {
                    admitted = true;
                    break;
                }
            }
            if (!admitted) {
                errno = ENOTCONN;
                return ZLINK_SUBMIT_NOT_CONNECTED;
            }
            lock.unlock ();
            const zlink_mesh_operation_id_t op_id = register_operation (
              node, ZLINK_MESH_OPERATION_ACTOR_JOIN, node_owner (), timeout_ms_);
            {
                std::lock_guard<std::mutex> relock (node->mutex);
                std::unordered_map<uint64_t, pending_operation_t>::iterator op_it =
                  node->operations.find (op_id.low);
                if (op_it != node->operations.end ())
                    op_it->second.join_actor = *actor_;
            }
            const zlink_submit_result_t rc = wire_submit_actor_join (
              node, target_node, op_id.low, *actor_, false, target_spot,
              target_spot_generation_, creation_parts_, creation_part_count_);
            if (rc != ZLINK_SUBMIT_OK) {
                std::lock_guard<std::mutex> relock (node->mutex);
                node->operations.erase (op_id.low);
                return rc;
            }
            if (schedule_operation_timeout (node, op_id.low, timeout_ms_) != 0)
                return ZLINK_SUBMIT_INTERNAL_ERROR;
            *operation_id_out_ = op_id;
            return ZLINK_SUBMIT_OK;
        }
        const std::string key (target_spot.begin (), target_spot.end ());
        std::map<std::string, spot_state_t>::iterator it = node->spots.find (key);
        if (it == node->spots.end () || it->second.generation != target_spot_generation_) {
            errno = ESTALE;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        target_owner = spot_owner (target_spot, it->second.generation);
    }

    const zlink_mesh_operation_id_t op_id =
      register_operation (node, ZLINK_MESH_OPERATION_ACTOR_JOIN, node_owner (), timeout_ms_);

    //  The join control record carries a one-shot reply token bound to the
    //  actor-join route kind.
    uint64_t reply_serial;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        reply_serial = node->next_reply_serial++;
        reply_route_t route;
        route.kind = reply_route_t::kind_actor_join;
        route.requester = node_owner ();
        route.requester_node_generation = node->lifecycle_generation;
        route.operation_id = op_id;
        route.operation_kind = ZLINK_MESH_OPERATION_ACTOR_JOIN;
        route.consumed = false;
        route.join_actor = *actor_;
        route.join_target_spot_rid = target_spot;
        route.join_target_spot_generation = target_spot_generation_;
        node->reply_routes[reply_serial] = route;
    }

    std::unique_ptr<queued_record_t> record =
      control_record (node, ZLINK_ACTOR_LIFECYCLE_JOINED, actor_snapshot, actor_snapshot,
                      ZLINK_REQUEST_OK);
    if (!record.get ()) {
        errno = ENOMEM;
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    }
    record->operation_id = op_id;
    record->operation_kind = ZLINK_MESH_OPERATION_ACTOR_JOIN;
    record->has_reply_token = true;
    seal_reply_token (node, reply_serial, &record->reply_token);
    for (size_t i = 0; i < creation_part_count_; ++i) {
        record->parts.push_back (zlink_msg_t ());
        zlink_msg_init (&record->parts.back ());
        if (zlink_msg_copy (&record->parts.back (), const_cast<zlink_msg_t *> (&creation_parts_[i]))
            != 0) {
            errno = EFAULT;
            return ZLINK_SUBMIT_INTERNAL_ERROR;
        }
        record->byte_size += zlink_msg_size (&record->parts.back ());
    }

    if (admit_record (node, target_owner, domain_application, record, timeout_ms_ > 0, timeout_ms_)
        != 0) {
        std::lock_guard<std::mutex> lock (node->mutex);
        node->operations.erase (op_id.low);
        node->reply_routes.erase (reply_serial);
        switch (errno) {
            case EAGAIN:
                return ZLINK_SUBMIT_BACKPRESSURED;
            case ETIMEDOUT:
                return ZLINK_SUBMIT_BACKPRESSURED;
            default:
                return ZLINK_SUBMIT_INVALID_STATE;
        }
    }
    *operation_id_out_ = op_id;
    return ZLINK_SUBMIT_OK;
}

zlink_submit_result_t zlink_mesh_node_actor_join_entry_spot (void *mesh_node_,
                                                             const zlink_actor_ref_t *actor_,
                                                             const zlink_routing_id_t *target_node_rid_,
                                                             const zlink_msg_t *creation_parts_,
                                                             size_t creation_part_count_,
                                                             zlink_mesh_operation_id_t *operation_id_out_,
                                                             uint32_t timeout_ms_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (!target_node_rid_ || target_node_rid_->size == 0) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    zlink_routing_id_t entry_spot_rid;
    uint64_t entry_generation = 0;
    {
        std::unique_lock<std::mutex> lock (node->mutex);
        const rid_bytes_t target_node = rid_bytes (*target_node_rid_);
        if (target_node != node->routing_id) {
            //  The target node resolves its own entry Spot during admission.
            if (check_actor_ref (actor_) != 0 || !operation_id_out_) {
                errno = EINVAL;
                return ZLINK_SUBMIT_INVALID_ARGUMENT;
            }
            int err = 0;
            if (!find_actor_locked (node, actor_, &err)) {
                errno = err;
                return err == ESTALE ? ZLINK_SUBMIT_INVALID_STATE : ZLINK_SUBMIT_NOT_FOUND;
            }
            bool admitted = false;
            for (size_t i = 0; i < node->peers.size (); ++i) {
                if (node->peers[i].state == ZLINK_MESH_PEER_ADMITTED
                    && node->peers[i].rid == target_node) {
                    admitted = true;
                    break;
                }
            }
            if (!admitted) {
                errno = ENOTCONN;
                return ZLINK_SUBMIT_NOT_CONNECTED;
            }
            lock.unlock ();
            const zlink_mesh_operation_id_t op_id = register_operation (
              node, ZLINK_MESH_OPERATION_ACTOR_JOIN, node_owner (), timeout_ms_);
            {
                std::lock_guard<std::mutex> relock (node->mutex);
                std::unordered_map<uint64_t, pending_operation_t>::iterator op_it =
                  node->operations.find (op_id.low);
                if (op_it != node->operations.end ())
                    op_it->second.join_actor = *actor_;
            }
            const zlink_submit_result_t rc = wire_submit_actor_join (
              node, target_node, op_id.low, *actor_, true, rid_bytes_t (), 0, creation_parts_,
              creation_part_count_);
            if (rc != ZLINK_SUBMIT_OK) {
                std::lock_guard<std::mutex> relock (node->mutex);
                node->operations.erase (op_id.low);
                return rc;
            }
            if (schedule_operation_timeout (node, op_id.low, timeout_ms_) != 0)
                return ZLINK_SUBMIT_INTERNAL_ERROR;
            *operation_id_out_ = op_id;
            return ZLINK_SUBMIT_OK;
        }
        const std::string entry_key (node->routing_id.begin (), node->routing_id.end ());
        std::map<std::string, spot_state_t>::iterator it = node->spots.find (entry_key);
        if (it == node->spots.end ()) {
            errno = EINVAL;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        entry_spot_rid = rid_value (it->second.rid);
        entry_generation = it->second.generation;
    }
    return zlink_mesh_node_actor_join_spot (mesh_node_, actor_, target_node_rid_, &entry_spot_rid,
                                            entry_generation, creation_parts_,
                                            creation_part_count_, operation_id_out_, timeout_ms_);
}

zlink_submit_result_t zlink_mesh_node_actor_leave_spot (void *mesh_node_,
                                                        const zlink_actor_ref_t *actor_,
                                                        uint64_t expected_membership_epoch_,
                                                        zlink_mesh_operation_id_t *operation_id_out_,
                                                        uint32_t timeout_ms_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (check_actor_ref (actor_) != 0 || !operation_id_out_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    actor_state_t before, after;
    owner_id_t previous_spot_owner;
    {
        std::unique_lock<std::mutex> lock (node->mutex);
        if (node->state == ZLINK_MESH_NODE_DRAINING || node->state == ZLINK_MESH_NODE_STOPPED) {
            errno = ESHUTDOWN;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        int err = 0;
        actor_state_t *actor = find_actor_locked (node, actor_, &err);
        if (!actor) {
            errno = err;
            return err == ESTALE ? ZLINK_SUBMIT_INVALID_STATE : ZLINK_SUBMIT_NOT_FOUND;
        }
        if (actor->membership_epoch != expected_membership_epoch_) {
            errno = ESTALE;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        before = *actor;
        previous_spot_owner = spot_owner (actor->spot_rid, actor->spot_generation);

        //  Leaving returns the actor to the entry Spot.
        const std::string entry_key (node->routing_id.begin (), node->routing_id.end ());
        std::map<std::string, spot_state_t>::iterator entry_it = node->spots.find (entry_key);
        if (entry_it == node->spots.end ()) {
            errno = EINVAL;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        const std::string prev_key (actor->spot_rid.begin (), actor->spot_rid.end ());
        std::map<std::string, spot_state_t>::iterator prev_it = node->spots.find (prev_key);
        if (prev_it != node->spots.end () && prev_it->second.active_actor_count > 0) {
            prev_it->second.active_actor_count -= 1;
            maybe_end_spot_locked (node, prev_key);
        }
        actor->spot_rid = entry_it->second.rid;
        actor->spot_generation = entry_it->second.generation;
        actor->membership_epoch += 1;
        entry_it->second.active_actor_count += 1;
        after = *actor;
    }

    const zlink_mesh_operation_id_t op_id =
      register_operation (node, ZLINK_MESH_OPERATION_ACTOR_LEAVE, node_owner (), timeout_ms_);
    *operation_id_out_ = op_id;

    std::unique_ptr<queued_record_t> record =
      control_record (node, ZLINK_ACTOR_LIFECYCLE_LEFT, before, after, ZLINK_REQUEST_OK);
    if (record.get ())
        (void) admit_record (node, previous_spot_owner, domain_infrastructure, record, false, 0);

    pending_operation_t completed;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        std::unordered_map<uint64_t, pending_operation_t>::iterator op_it =
          node->operations.find (op_id.low);
        if (op_it == node->operations.end ())
            return ZLINK_SUBMIT_OK;
        completed = op_it->second;
        node->operations.erase (op_it);
    }
    complete_operation (node, completed, ZLINK_REQUEST_OK, 0, NULL, NULL);
    return ZLINK_SUBMIT_OK;
}

zlink_submit_result_t zlink_actor_join_reply (const zlink_mesh_reply_token_t *token_,
                                              zlink_actor_join_result_t join_result_,
                                              const zlink_msg_t *parts_,
                                              size_t part_count_,
                                              zlink_send_flags_t flags_)
{
    LIBZLINK_UNUSED (flags_);
    if (join_result_ != ZLINK_ACTOR_JOIN_ACCEPTED && join_result_ != ZLINK_ACTOR_JOIN_REJECTED) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (part_count_ > 0 && !parts_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    mesh_node_t *node_ptr = NULL;
    uint64_t serial = 0;
    if (unseal_reply_token (token_, &node_ptr, &serial) != 0)
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    mesh_node_t *node = as_mesh_node (node_ptr);
    if (!node) {
        errno = ESHUTDOWN;
        return ZLINK_SUBMIT_INVALID_STATE;
    }

    pending_operation_t op;
    zlink_actor_join_completion_t completion;
    memset (&completion, 0, sizeof (completion));
    completion.struct_size = sizeof (completion);
    completion.version = 1;
    completion.join_result = join_result_;
    bool remote = false;
    rid_bytes_t remote_origin;
    uint64_t remote_correlation = 0;
    rid_bytes_t remote_joined_spot;
    uint64_t remote_joined_generation = 0;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        std::unordered_map<uint64_t, reply_route_t>::iterator it = node->reply_routes.find (serial);
        if (it == node->reply_routes.end ()) {
            errno = ESTALE;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        reply_route_t &route = it->second;
        if (route.kind != reply_route_t::kind_actor_join) {
            errno = EINVAL;
            return ZLINK_SUBMIT_INVALID_ARGUMENT;
        }
        if (route.consumed) {
            errno = EALREADY;
            return ZLINK_SUBMIT_INVALID_STATE;
        }

        if (route.remote_origin) {
            //  The joining actor lives on the requesting peer; the accepted
            //  verdict only commits this Spot's active count here and the
            //  membership commit happens at the source on the wire reply.
            if (join_result_ == ZLINK_ACTOR_JOIN_ACCEPTED) {
                const std::string spot_key (route.join_target_spot_rid.begin (),
                                            route.join_target_spot_rid.end ());
                std::map<std::string, spot_state_t>::iterator spot_it =
                  node->spots.find (spot_key);
                if (spot_it != node->spots.end ()
                    && spot_it->second.generation == route.join_target_spot_generation)
                    spot_it->second.active_actor_count += 1;
            }
            route.consumed = true;
            remote = true;
            remote_origin = route.origin_rid;
            remote_correlation = route.origin_correlation;
            remote_joined_spot = route.join_target_spot_rid;
            remote_joined_generation = route.join_target_spot_generation;
        }

        if (!remote) {
        const std::string id (route.join_actor.actor_id);
        std::map<std::string, actor_state_t>::iterator actor_it = node->actors.find (id);
        if (actor_it == node->actors.end ()
            || actor_it->second.generation != route.join_actor.generation) {
            errno = ESTALE;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        actor_state_t &actor = actor_it->second;

        if (join_result_ == ZLINK_ACTOR_JOIN_ACCEPTED) {
            //  The accepted reply is the single membership commit point.
            const std::string prev_key (actor.spot_rid.begin (), actor.spot_rid.end ());
            std::map<std::string, spot_state_t>::iterator prev_it = node->spots.find (prev_key);
            if (prev_it != node->spots.end () && prev_it->second.active_actor_count > 0) {
                prev_it->second.active_actor_count -= 1;
                maybe_end_spot_locked (node, prev_key);
            }
            actor.spot_rid = route.join_target_spot_rid;
            actor.spot_generation = route.join_target_spot_generation;
            actor.membership_epoch += 1;
            const std::string new_key (actor.spot_rid.begin (), actor.spot_rid.end ());
            std::map<std::string, spot_state_t>::iterator new_it = node->spots.find (new_key);
            if (new_it != node->spots.end ())
                new_it->second.active_actor_count += 1;
        }

        completion.actor.node_rid = rid_value (node->routing_id);
        snprintf (completion.actor.actor_id, sizeof (completion.actor.actor_id), "%s",
                  id.c_str ());
        completion.actor.generation = actor.generation;
        completion.location.struct_size = sizeof (completion.location);
        completion.location.version = 1;
        completion.location.actor = completion.actor;
        completion.location.spot_rid = rid_value (actor.spot_rid);
        completion.location.spot_generation = actor.spot_generation;
        completion.location.membership_epoch = actor.membership_epoch;

        route.consumed = true;
        std::unordered_map<uint64_t, pending_operation_t>::iterator op_it =
          node->operations.find (route.operation_id.low);
        if (op_it == node->operations.end ())
            return ZLINK_SUBMIT_OK;
        op = op_it->second;
        node->operations.erase (op_it);
        }
    }

    if (remote)
        return wire_submit_join_reply (node, remote_origin, remote_correlation, join_result_,
                                       remote_joined_spot, remote_joined_generation, parts_,
                                       part_count_);

    std::vector<unsigned char> kind_data (reinterpret_cast<unsigned char *> (&completion),
                                          reinterpret_cast<unsigned char *> (&completion)
                                            + sizeof (completion));
    std::vector<zlink_msg_t> reply_parts;
    if (part_count_ > 0) {
        reply_parts.resize (part_count_);
        for (size_t i = 0; i < part_count_; ++i) {
            zlink_msg_init (&reply_parts[i]);
            if (zlink_msg_copy (&reply_parts[i], const_cast<zlink_msg_t *> (&parts_[i])) != 0) {
                errno = EFAULT;
                return ZLINK_SUBMIT_INTERNAL_ERROR;
            }
        }
    }
    if (join_result_ == ZLINK_ACTOR_JOIN_ACCEPTED)
        complete_operation (node, op, ZLINK_REQUEST_OK, 0, &kind_data, &reply_parts);
    else
        complete_operation (node, op, ZLINK_REQUEST_REJECTED, EACCES, &kind_data, &reply_parts);
    return ZLINK_SUBMIT_OK;
}

//  --- messaging ---------------------------------------------------------------------

namespace
{
zlink_submit_result_t actor_submit (void *mesh_node_,
                                    const zlink_actor_ref_t *source_actor_,
                                    const zlink_actor_ref_t *target_actor_,
                                    const zlink_mesh_metadata_view_t *metadata_,
                                    const zlink_msg_t *parts_,
                                    size_t part_count_,
                                    zlink_mesh_operation_id_t *operation_id_out_,
                                    zlink_send_flags_t flags_,
                                    uint32_t timeout_ms_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (check_actor_ref (target_actor_) != 0) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!parts_ || part_count_ == 0) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (metadata_) {
        if (!metadata_->data || metadata_->size == 0
            || zlink::mesh::validate_metadata (metadata_->data, metadata_->size) != 0) {
            errno = EINVAL;
            return ZLINK_SUBMIT_INVALID_ARGUMENT;
        }
    }
    if (operation_id_out_)
        memset (operation_id_out_, 0, sizeof (*operation_id_out_));

    owner_id_t requester = node_owner ();
    owner_id_t destination;
    {
        std::unique_lock<std::mutex> lock (node->mutex);
        if (node->state == ZLINK_MESH_NODE_DRAINING || node->state == ZLINK_MESH_NODE_STOPPED) {
            errno = ESHUTDOWN;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        if (source_actor_) {
            int err = 0;
            if (check_actor_ref (source_actor_) != 0
                || !find_actor_locked (node, source_actor_, &err)) {
                errno = err ? err : EINVAL;
                return err == ESTALE ? ZLINK_SUBMIT_INVALID_STATE : ZLINK_SUBMIT_INVALID_ARGUMENT;
            }
            requester = actor_owner (source_actor_->actor_id, source_actor_->generation);
        }
        if (!rid_equal (target_actor_->node_rid, rid_value (node->routing_id))) {
            const rid_bytes_t target_node = rid_bytes (target_actor_->node_rid);
            bool admitted = false;
            for (size_t i = 0; i < node->peers.size (); ++i) {
                if (node->peers[i].state == ZLINK_MESH_PEER_ADMITTED
                    && node->peers[i].rid == target_node) {
                    admitted = true;
                    break;
                }
            }
            if (!admitted) {
                errno = ENOTCONN;
                return ZLINK_SUBMIT_NOT_CONNECTED;
            }
            lock.unlock ();
            //  Remote actor submit: the reply returns over the wire keyed by
            //  the operation serial.
            if (!operation_id_out_) {
                return wire_submit_actor_data (node, target_node, false, 0, source_actor_,
                                               *target_actor_, metadata_, parts_, part_count_,
                                               flags_);
            }
            const zlink_mesh_operation_id_t op_id = register_operation (
              node, ZLINK_MESH_OPERATION_ACTOR_REQUEST, requester, timeout_ms_);
            const zlink_submit_result_t rc =
              wire_submit_actor_data (node, target_node, true, op_id.low, source_actor_,
                                      *target_actor_, metadata_, parts_, part_count_, flags_);
            if (rc != ZLINK_SUBMIT_OK) {
                std::lock_guard<std::mutex> relock (node->mutex);
                node->operations.erase (op_id.low);
                return rc;
            }
            if (schedule_operation_timeout (node, op_id.low, timeout_ms_) != 0)
                return ZLINK_SUBMIT_INTERNAL_ERROR;
            *operation_id_out_ = op_id;
            return ZLINK_SUBMIT_OK;
        }
        int err = 0;
        actor_state_t *target = find_actor_locked (node, target_actor_, &err);
        if (!target) {
            errno = err;
            return err == ESTALE ? ZLINK_SUBMIT_INVALID_STATE : ZLINK_SUBMIT_NOT_FOUND;
        }
        destination = actor_owner (target->id, target->generation);
    }

    std::unique_ptr<queued_record_t> record (new (std::nothrow) queued_record_t ());
    if (!record.get ()) {
        errno = ENOMEM;
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    }
    const bool is_request = operation_id_out_ != NULL;
    record->kind = is_request ? ZLINK_MESH_RECORD_ACTOR_REQUEST : ZLINK_MESH_RECORD_ACTOR_SEND;
    record->source_node_rid = node->routing_id;
    if (source_actor_)
        record->source_actor = *source_actor_;
    if (metadata_) {
        record->has_metadata = true;
        record->application_metadata.assign (metadata_->data, metadata_->data + metadata_->size);
        record->byte_size += metadata_->size;
    }
    record->parts.resize (part_count_);
    for (size_t i = 0; i < part_count_; ++i) {
        zlink_msg_init (&record->parts[i]);
        if (zlink_msg_copy (&record->parts[i], const_cast<zlink_msg_t *> (&parts_[i])) != 0) {
            errno = EFAULT;
            return ZLINK_SUBMIT_INTERNAL_ERROR;
        }
        record->byte_size += zlink_msg_size (&record->parts[i]);
    }

    zlink_mesh_operation_id_t op_id;
    memset (&op_id, 0, sizeof (op_id));
    uint64_t reply_serial = 0;
    if (is_request) {
        op_id = register_operation (node, ZLINK_MESH_OPERATION_ACTOR_REQUEST, requester,
                                    timeout_ms_);
        std::lock_guard<std::mutex> lock (node->mutex);
        reply_serial = node->next_reply_serial++;
        reply_route_t route;
        route.kind = reply_route_t::kind_generic;
        route.requester = requester;
        route.requester_node_generation = node->lifecycle_generation;
        route.operation_id = op_id;
        route.operation_kind = ZLINK_MESH_OPERATION_ACTOR_REQUEST;
        route.consumed = false;
        memset (&route.join_actor, 0, sizeof (route.join_actor));
        route.join_target_spot_generation = 0;
        node->reply_routes[reply_serial] = route;
        record->operation_id = op_id;
        record->operation_kind = ZLINK_MESH_OPERATION_ACTOR_REQUEST;
        record->has_reply_token = true;
        seal_reply_token (node, reply_serial, &record->reply_token);
    }

    const bool blocking = (flags_ & ZLINK_SEND_FLAGS_DONTWAIT) == 0;
    if (admit_record (node, destination, domain_application, record, blocking,
                      blocking ? static_cast<uint32_t> (node->sndtimeo_ms < 0 ? 0 : node->sndtimeo_ms)
                               : 0)
        != 0) {
        if (is_request) {
            std::lock_guard<std::mutex> lock (node->mutex);
            node->operations.erase (op_id.low);
            node->reply_routes.erase (reply_serial);
        }
        switch (errno) {
            case EAGAIN:
            case ETIMEDOUT:
                return ZLINK_SUBMIT_BACKPRESSURED;
            case ESHUTDOWN:
                return ZLINK_SUBMIT_INVALID_STATE;
            default:
                return ZLINK_SUBMIT_INTERNAL_ERROR;
        }
    }
    if (is_request)
        *operation_id_out_ = op_id;
    return ZLINK_SUBMIT_OK;
}
}

zlink_submit_result_t zlink_mesh_node_send_to_actor (void *mesh_node_,
                                                     const zlink_actor_ref_t *actor_,
                                                     const zlink_mesh_metadata_view_t *actor_metadata_,
                                                     const zlink_msg_t *parts_,
                                                     size_t part_count_,
                                                     zlink_send_flags_t flags_)
{
    return actor_submit (mesh_node_, NULL, actor_, actor_metadata_, parts_, part_count_, NULL,
                         flags_, 0);
}

zlink_submit_result_t zlink_mesh_node_request_to_actor (void *mesh_node_,
                                                        const zlink_actor_ref_t *actor_,
                                                        const zlink_mesh_metadata_view_t *actor_metadata_,
                                                        const zlink_msg_t *parts_,
                                                        size_t part_count_,
                                                        zlink_mesh_operation_id_t *operation_id_out_,
                                                        zlink_send_flags_t flags_,
                                                        uint32_t timeout_ms_)
{
    if (!operation_id_out_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    return actor_submit (mesh_node_, NULL, actor_, actor_metadata_, parts_, part_count_,
                         operation_id_out_, flags_, timeout_ms_);
}

zlink_submit_result_t zlink_actor_send_to_actor (void *mesh_node_,
                                                 const zlink_actor_ref_t *source_actor_,
                                                 const zlink_actor_ref_t *target_actor_,
                                                 const zlink_mesh_metadata_view_t *actor_metadata_,
                                                 const zlink_msg_t *parts_,
                                                 size_t part_count_,
                                                 zlink_send_flags_t flags_)
{
    if (!source_actor_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    return actor_submit (mesh_node_, source_actor_, target_actor_, actor_metadata_, parts_,
                         part_count_, NULL, flags_, 0);
}

zlink_submit_result_t zlink_actor_request_to_actor (void *mesh_node_,
                                                    const zlink_actor_ref_t *source_actor_,
                                                    const zlink_actor_ref_t *target_actor_,
                                                    const zlink_mesh_metadata_view_t *actor_metadata_,
                                                    const zlink_msg_t *parts_,
                                                    size_t part_count_,
                                                    zlink_mesh_operation_id_t *operation_id_out_,
                                                    zlink_send_flags_t flags_,
                                                    uint32_t timeout_ms_)
{
    if (!source_actor_ || !operation_id_out_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    return actor_submit (mesh_node_, source_actor_, target_actor_, actor_metadata_, parts_,
                         part_count_, operation_id_out_, flags_, timeout_ms_);
}
