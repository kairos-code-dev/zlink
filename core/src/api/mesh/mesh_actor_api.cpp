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
    actor_state_t before;
    actor_state_t after;
    bool stale = false;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        const std::string id (op_.join_actor.actor_id);
        std::map<std::string, actor_state_t>::iterator it = node_->actors.find (id);
        if (it == node_->actors.end ()
            || it->second.generation != op_.join_actor.generation) {
            stale = true;
        } else {
            before = it->second;
            after = before;
            if (join_result_ == ZLINK_ACTOR_JOIN_ACCEPTED) {
                after.spot_rid = spot_rid_;
                after.spot_generation = spot_generation_;
                after.spot_node_rid = spot_node_rid_;
                after.membership_epoch += 1;
            }
        }
    }
    if (stale) {
        (void) complete_pending_operation (node_, op_, ZLINK_REQUEST_CONFLICT, ESTALE, NULL,
                                           NULL);
        return;
    }

    zlink_actor_join_completion_t completion;
    memset (&completion, 0, sizeof (completion));
    completion.struct_size = sizeof (completion);
    completion.version = 1;
    completion.join_result = static_cast<zlink_actor_join_result_t> (join_result_);
    completion.actor.node_rid = rid_value (node_->routing_id);
    snprintf (completion.actor.actor_id, sizeof (completion.actor.actor_id), "%s",
              after.id.c_str ());
    completion.actor.generation = after.generation;
    completion.location.struct_size = sizeof (completion.location);
    completion.location.version = 1;
    completion.location.actor = completion.actor;
    completion.location.spot_rid = rid_value (after.spot_rid);
    completion.location.spot_generation = after.spot_generation;
    completion.location.membership_epoch = after.membership_epoch;
    std::vector<unsigned char> kind_data (
      reinterpret_cast<unsigned char *> (&completion),
      reinterpret_cast<unsigned char *> (&completion) + sizeof (completion));

    owner_id_t ready_owner = op_.requester;
    const std::pair<owner_id_t, int> ready_key (
      ready_owner, static_cast<int> (domain_infrastructure));
    rid_bytes_t left_notify_node;
    zlink_actor_ref_t left_actor;
    rid_bytes_t left_prev_spot;
    uint64_t left_prev_generation = 0;
    uint64_t left_prev_epoch = 0;
    uint64_t left_new_epoch = 0;
    const bool previous_spot_remote = !before.spot_node_rid.empty ();
    const std::string previous_spot_key (
      before.spot_rid.begin (), before.spot_rid.end ());
    if (join_result_ == ZLINK_ACTOR_JOIN_ACCEPTED
        && previous_spot_remote) {
        left_notify_node = before.spot_node_rid;
        left_actor = op_.join_actor;
        left_prev_spot = before.spot_rid;
        left_prev_generation = before.spot_generation;
        left_prev_epoch = before.membership_epoch;
    }
    const int32_t terminal_result =
      join_result_ == ZLINK_ACTOR_JOIN_ACCEPTED ? ZLINK_REQUEST_OK
                                                : ZLINK_REQUEST_REJECTED;
    const int32_t failure_errno =
      join_result_ == ZLINK_ACTOR_JOIN_ACCEPTED ? 0 : EACCES;
    bool delivered = false;
    //  Cancelled outside the node mutex (same contract as
    //  complete_pending_operation_with_commit).
    std::shared_ptr<zlink::request_timeout::task_t> timeout_task;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        std::unordered_map<uint64_t, pending_operation_t>::iterator op_it =
          node_->operations.find (op_.id.low);
        std::map<std::string, actor_state_t>::iterator actor_it =
          node_->actors.find (before.id);
        if (op_it == node_->operations.end ()
            || actor_it == node_->actors.end ()
            || actor_it->second.generation != before.generation
            || actor_it->second.membership_epoch != before.membership_epoch
            || actor_it->second.spot_rid != before.spot_rid
            || actor_it->second.spot_node_rid != before.spot_node_rid
            || actor_it->second.spot_generation != before.spot_generation)
            return;
        std::map<owner_id_t, owner_state_t>::iterator owner_it =
          node_->owners.find (op_it->second.requester);
        const std::shared_ptr<completion_reservation_t> reservation =
          op_it->second.completion;
        if (owner_it == node_->owners.end () || !reservation
            || reservation->records.empty ())
            return;

        mailbox_t &mailbox =
          owner_it->second.domains[domain_infrastructure];
        bool ready_inserted = false;
        try {
#ifdef ZLINK_BUILD_TESTS
            test_maybe_throw_alloc ();
#endif
            ready_inserted = node_->ready.insert (ready_key).second;
        }
        catch (const std::bad_alloc &) {
            if (ready_inserted)
                node_->ready.erase (ready_key);
            return;
        }
        queued_record_t &record = *reservation->records.front ();
        if (!reservation->prepared) {
            record.kind = ZLINK_MESH_RECORD_COMPLETION;
            record.operation_id = op_.id;
            record.operation_kind = op_.kind;
            record.terminal_result = terminal_result;
            record.failure_errno = failure_errno;
            record.kind_data.swap (kind_data);
            reservation->prepared = true;
        }
        mailbox.records.splice (mailbox.records.end (),
                                reservation->records);
        mailbox.pending_messages += 1;
        mailbox.pending_bytes += record.byte_size;

        if (join_result_ == ZLINK_ACTOR_JOIN_ACCEPTED) {
            //  Completion storage and membership change commit together.
            if (!previous_spot_remote) {
                std::map<std::string, spot_state_t>::iterator prev_it =
                  node_->spots.find (previous_spot_key);
                if (prev_it != node_->spots.end ()
                    && prev_it->second.active_actor_count > 0) {
                    prev_it->second.active_actor_count -= 1;
                    maybe_end_spot_locked (node_, previous_spot_key);
                }
            }
            actor_it->second = std::move (after);
            left_new_epoch = actor_it->second.membership_epoch;
        }
        timeout_task = op_it->second.timeout_task;
        node_->operations.erase (op_it);
        if (node_->monitor)
            node_->monitor->counters.completed_operations += 1;
        delivered = true;
    }
    if (timeout_task)
        zlink::request_timeout::cancel (timeout_task);
    if (!delivered)
        return;
    signal_ready (node_, ready_owner, domain_infrastructure);
    zlink_mesh_monitor_event_t event;
    memset (&event, 0, sizeof (event));
    event.kind = ZLINK_MESH_MONITOR_OPERATION_COMPLETED;
    event.operation_id_high = op_.id.high;
    event.operation_id_low = op_.id.low;
    event.result_code = terminal_result;
    event.failure_errno = failure_errno;
    emit_monitor_event (node_, event);
    if (!left_notify_node.empty ())
        wire_notify_actor_left (node_, left_notify_node, left_actor, left_prev_spot,
                                left_prev_generation, left_prev_epoch, left_new_epoch);
}
}
}

namespace
{
struct actor_destroy_commit_t
{
    std::string actor_id;
    uint64_t generation;
    std::string spot_key;
    owner_id_t actor_owner_id;
    bool spot_present;
};

bool commit_actor_destroy_locked (mesh_node_t *node_, void *userdata_)
{
    actor_destroy_commit_t *commit =
      static_cast<actor_destroy_commit_t *> (userdata_);
    std::map<std::string, actor_state_t>::iterator actor_it =
      node_->actors.find (commit->actor_id);
    if (actor_it == node_->actors.end ()
        || actor_it->second.generation != commit->generation)
        return false;
    std::map<std::string, spot_state_t>::iterator spot_it =
      node_->spots.find (commit->spot_key);
    commit->spot_present = spot_it != node_->spots.end ();
    if (commit->spot_present && spot_it->second.active_actor_count > 0) {
        spot_it->second.active_actor_count -= 1;
        maybe_end_spot_locked (node_, commit->spot_key);
    }
    node_->owners.erase (commit->actor_owner_id);
    node_->actors.erase (actor_it);
    return true;
}

struct actor_leave_commit_t
{
    std::string actor_id;
    uint64_t generation;
    uint64_t expected_epoch;
    std::string previous_spot_key;
    std::string entry_spot_key;
    actor_state_t after;
};

bool commit_actor_leave_locked (mesh_node_t *node_, void *userdata_)
{
    actor_leave_commit_t *commit =
      static_cast<actor_leave_commit_t *> (userdata_);
    std::map<std::string, actor_state_t>::iterator actor_it =
      node_->actors.find (commit->actor_id);
    if (actor_it == node_->actors.end ()
        || actor_it->second.generation != commit->generation
        || actor_it->second.membership_epoch != commit->expected_epoch)
        return false;
    std::map<std::string, spot_state_t>::iterator previous =
      node_->spots.find (commit->previous_spot_key);
    if (previous != node_->spots.end ()
        && previous->second.active_actor_count > 0) {
        previous->second.active_actor_count -= 1;
        maybe_end_spot_locked (node_, commit->previous_spot_key);
    }
    std::map<std::string, spot_state_t>::iterator entry =
      node_->spots.find (commit->entry_spot_key);
    if (entry != node_->spots.end ())
        entry->second.active_actor_count += 1;
    actor_it->second = std::move (commit->after);
    return true;
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
    mesh_node_pin_t node_pin (mesh_node_);
    mesh_node_t *node = node_pin.get ();
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
    mesh_node_pin_t node_pin (mesh_node_);
    mesh_node_t *node = node_pin.get ();
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
try {
    mesh_node_pin_t node_pin (mesh_node_);
    mesh_node_t *node = node_pin.get ();
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
    operation_submission_t submission (
      node, true, ZLINK_MESH_OPERATION_ACTOR_LOOKUP, node_owner (), timeout_ms_);
    if (!submission.valid ())
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    const zlink_mesh_operation_id_t op_id = submission.operation_id ();
    const zlink_submit_result_t rc = wire_submit_actor_lookup (node, target_node, op_id.low, id);
    if (rc != ZLINK_SUBMIT_OK)
        return rc;
    submission.commit ();
    *operation_id_out_ = op_id;
    return ZLINK_SUBMIT_OK;
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}

zlink_submit_result_t zlink_mesh_node_actor_destroy (void *mesh_node_,
                                                     const zlink_actor_ref_t *actor_,
                                                     zlink_mesh_operation_id_t *operation_id_out_,
                                                     uint32_t timeout_ms_)
try {
    mesh_node_pin_t node_pin (mesh_node_);
    mesh_node_t *node = node_pin.get ();
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
            operation_submission_t submission (
              node, true, ZLINK_MESH_OPERATION_ACTOR_DESTROY, node_owner (), timeout_ms_);
            if (!submission.valid ())
                return ZLINK_SUBMIT_OUT_OF_MEMORY;
            const zlink_mesh_operation_id_t op_id = submission.operation_id ();
            const zlink_submit_result_t rc =
              wire_submit_actor_destroy (node, target_node, op_id.low, *actor_);
            if (rc != ZLINK_SUBMIT_OK)
                return rc;
            submission.commit ();
            *operation_id_out_ = op_id;
            return ZLINK_SUBMIT_OK;
        }
    }

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
    }

    operation_submission_t submission (
      node, true, ZLINK_MESH_OPERATION_ACTOR_DESTROY, node_owner (), 0);
    if (!submission.valid ())
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    const zlink_mesh_operation_id_t op_id = submission.operation_id ();
    actor_destroy_commit_t destroy_commit;
    destroy_commit.actor_id = before.id;
    destroy_commit.generation = before.generation;
    destroy_commit.spot_key.assign (
      before.spot_rid.begin (), before.spot_rid.end ());
    destroy_commit.actor_owner_id =
      actor_owner (before.id, before.generation);
    destroy_commit.spot_present = false;
    std::unique_ptr<queued_record_t> destroyed_record = control_record (
      node, ZLINK_ACTOR_LIFECYCLE_DESTROYED, before, before,
      ZLINK_REQUEST_OK);
    if (!destroyed_record.get ())
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        std::map<std::string, actor_state_t>::iterator actor_it =
          node->actors.find (before.id);
        if (actor_it == node->actors.end ()
            || actor_it->second.generation != before.generation
            || actor_it->second.draining) {
            errno = ESTALE;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        actor_it->second.draining = true;
    }

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

    pending_operation_t completed;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        std::unordered_map<uint64_t, pending_operation_t>::iterator op_it =
          node->operations.find (op_id.low);
        if (op_it == node->operations.end ()) {
            std::map<std::string, actor_state_t>::iterator actor_it =
              node->actors.find (before.id);
            if (actor_it != node->actors.end ()
                && actor_it->second.generation == before.generation)
                actor_it->second.draining = false;
            errno = ESHUTDOWN;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        completed = op_it->second;
    }
    const int completion_rc = complete_pending_operation_with_commit (
      node, completed, ZLINK_REQUEST_OK, 0, NULL, NULL,
      &commit_actor_destroy_locked, &destroy_commit);
    if (completion_rc < 0) {
        std::lock_guard<std::mutex> lock (node->mutex);
        std::map<std::string, actor_state_t>::iterator actor_it =
          node->actors.find (before.id);
        if (actor_it != node->actors.end ()
            && actor_it->second.generation == before.generation)
            actor_it->second.draining = false;
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    }
    if (completion_rc == 0) {
        errno = ESTALE;
        return ZLINK_SUBMIT_INVALID_STATE;
    }

    submission.commit ();
    *operation_id_out_ = op_id;
    if (destroy_commit.spot_present) {
        //  An ended Spot has no observer; otherwise emit the already prepared
        //  lifecycle record after the atomic destroy/completion commit.
        const owner_id_t spot_owner_id =
          spot_owner (before.spot_rid, before.spot_generation);
        (void) admit_record (
          node, spot_owner_id, domain_infrastructure, destroyed_record, false,
          0);
    }
    //  No session may keep addressing the destroyed generation.
    session_bindings_remove_actor (node, *actor_);
    return ZLINK_SUBMIT_OK;
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
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
try {
    mesh_node_pin_t node_pin (mesh_node_);
    mesh_node_t *node = node_pin.get ();
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
            operation_submission_t submission (
              node, true, ZLINK_MESH_OPERATION_ACTOR_JOIN, node_owner (), timeout_ms_);
            if (!submission.valid ())
                return ZLINK_SUBMIT_OUT_OF_MEMORY;
            const zlink_mesh_operation_id_t op_id = submission.operation_id ();
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
            if (rc != ZLINK_SUBMIT_OK)
                return rc;
            submission.commit ();
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

    operation_submission_t submission (
      node, true, ZLINK_MESH_OPERATION_ACTOR_JOIN, node_owner (), timeout_ms_);
    if (!submission.valid ())
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    const zlink_mesh_operation_id_t op_id = submission.operation_id ();

    //  The join control record carries a one-shot reply token bound to the
    //  actor-join route kind.
    uint64_t reply_serial;
    reply_route_t route;
    route.kind = reply_route_t::kind_actor_join;
    route.requester = node_owner ();
    route.operation_kind = ZLINK_MESH_OPERATION_ACTOR_JOIN;
    route.join_actor = *actor_;
    route.join_target_spot_rid = target_spot;
    route.join_target_spot_generation = target_spot_generation_;
    if (!submission.add_reply_route (route, &reply_serial))
        return ZLINK_SUBMIT_OUT_OF_MEMORY;

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
        switch (errno) {
            case EAGAIN:
                return ZLINK_SUBMIT_BACKPRESSURED;
            case ETIMEDOUT:
                return ZLINK_SUBMIT_BACKPRESSURED;
            default:
                return ZLINK_SUBMIT_INVALID_STATE;
        }
    }
    submission.commit ();
    *operation_id_out_ = op_id;
    return ZLINK_SUBMIT_OK;
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}

zlink_submit_result_t zlink_mesh_node_actor_join_entry_spot (void *mesh_node_,
                                                             const zlink_actor_ref_t *actor_,
                                                             const zlink_routing_id_t *target_node_rid_,
                                                             const zlink_msg_t *creation_parts_,
                                                             size_t creation_part_count_,
                                                             zlink_mesh_operation_id_t *operation_id_out_,
                                                             uint32_t timeout_ms_)
try {
    mesh_node_pin_t node_pin (mesh_node_);
    mesh_node_t *node = node_pin.get ();
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
            operation_submission_t submission (
              node, true, ZLINK_MESH_OPERATION_ACTOR_JOIN, node_owner (), timeout_ms_);
            if (!submission.valid ())
                return ZLINK_SUBMIT_OUT_OF_MEMORY;
            const zlink_mesh_operation_id_t op_id = submission.operation_id ();
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
            if (rc != ZLINK_SUBMIT_OK)
                return rc;
            submission.commit ();
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
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}

zlink_submit_result_t zlink_mesh_node_actor_leave_spot (void *mesh_node_,
                                                        const zlink_actor_ref_t *actor_,
                                                        uint64_t expected_membership_epoch_,
                                                        zlink_mesh_operation_id_t *operation_id_out_,
                                                        uint32_t timeout_ms_)
try {
    mesh_node_pin_t node_pin (mesh_node_);
    mesh_node_t *node = node_pin.get ();
    if (!node) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (check_actor_ref (actor_) != 0 || !operation_id_out_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    LIBZLINK_UNUSED (timeout_ms_);
    actor_state_t before, after;
    owner_id_t previous_spot_owner;
    actor_leave_commit_t leave_commit;
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
        after = before;
        previous_spot_owner = spot_owner (actor->spot_rid, actor->spot_generation);

        //  Leaving returns the actor to the entry Spot.
        const std::string entry_key (node->routing_id.begin (), node->routing_id.end ());
        std::map<std::string, spot_state_t>::iterator entry_it = node->spots.find (entry_key);
        if (entry_it == node->spots.end ()) {
            errno = EINVAL;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        const std::string prev_key (actor->spot_rid.begin (), actor->spot_rid.end ());
        after.spot_rid = entry_it->second.rid;
        after.spot_generation = entry_it->second.generation;
        after.membership_epoch += 1;
        leave_commit.actor_id = before.id;
        leave_commit.generation = before.generation;
        leave_commit.expected_epoch = before.membership_epoch;
        leave_commit.previous_spot_key = prev_key;
        leave_commit.entry_spot_key = entry_key;
        leave_commit.after = after;
    }

    operation_submission_t submission (
      node, true, ZLINK_MESH_OPERATION_ACTOR_LEAVE, node_owner (), 0);
    if (!submission.valid ())
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    const zlink_mesh_operation_id_t op_id = submission.operation_id ();
    std::unique_ptr<queued_record_t> record =
      control_record (node, ZLINK_ACTOR_LIFECYCLE_LEFT, before, after, ZLINK_REQUEST_OK);
    if (!record.get ())
        return ZLINK_SUBMIT_OUT_OF_MEMORY;

    pending_operation_t completed;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        std::unordered_map<uint64_t, pending_operation_t>::iterator op_it =
          node->operations.find (op_id.low);
        if (op_it == node->operations.end ()) {
            errno = ESHUTDOWN;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        completed = op_it->second;
    }
    const int completion_rc = complete_pending_operation_with_commit (
      node, completed, ZLINK_REQUEST_OK, 0, NULL, NULL,
      &commit_actor_leave_locked, &leave_commit);
    if (completion_rc < 0)
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    if (completion_rc == 0) {
        errno = ESTALE;
        return ZLINK_SUBMIT_INVALID_STATE;
    }
    submission.commit ();
    *operation_id_out_ = op_id;
    (void) admit_record (
      node, previous_spot_owner, domain_infrastructure, record, false, 0);
    return ZLINK_SUBMIT_OK;
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}

zlink_submit_result_t zlink_actor_join_reply (const zlink_mesh_reply_token_t *token_,
                                              zlink_actor_join_result_t join_result_,
                                              const zlink_msg_t *parts_,
                                              size_t part_count_,
                                              zlink_send_flags_t flags_)
try {
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
    mesh_node_pin_t node_pin (node_ptr);
    mesh_node_t *node = node_pin.get ();
    if (!node) {
        errno = ESHUTDOWN;
        return ZLINK_SUBMIT_INVALID_STATE;
    }

#ifdef ZLINK_BUILD_TESTS
    test_maybe_throw_alloc ();
#endif
    std::unique_ptr<queued_record_t> completion_record (
      new (std::nothrow) queued_record_t ());
    if (!completion_record.get ()) {
        errno = ENOMEM;
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    }
    completion_record->kind = ZLINK_MESH_RECORD_COMPLETION;
    completion_record->kind_data.resize (sizeof (zlink_actor_join_completion_t));
    completion_record->parts.resize (part_count_);
    for (size_t i = 0; i < part_count_; ++i) {
        zlink_msg_init (&completion_record->parts[i]);
        if (zlink_msg_copy (&completion_record->parts[i],
                            const_cast<zlink_msg_t *> (&parts_[i]))
            != 0) {
            errno = EFAULT;
            return ZLINK_SUBMIT_INTERNAL_ERROR;
        }
        completion_record->byte_size += zlink_msg_size (&completion_record->parts[i]);
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
    std::string remote_spot_key;
    owner_id_t ready_owner;
    bool deliver_local = false;
    bool consumed_without_owner = false;
    //  Cancelled outside the node mutex (same contract as
    //  complete_pending_operation_with_commit).
    std::shared_ptr<zlink::request_timeout::task_t> join_timeout_task;
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
        if (route.in_flight) {
            errno = EBUSY;
            return ZLINK_SUBMIT_INVALID_STATE;
        }

        if (route.remote_origin) {
            //  Reserve the token, but commit Spot state only after the wire
            //  reply has been accepted. A failed submit clears in_flight and
            //  leaves the token retryable.
            remote_spot_key.assign (route.join_target_spot_rid.begin (),
                                    route.join_target_spot_rid.end ());
            route.in_flight = true;
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
            actor_state_t after = actor;
            const std::string prev_key (actor.spot_rid.begin (), actor.spot_rid.end ());
            const std::string new_key (route.join_target_spot_rid.begin (),
                                       route.join_target_spot_rid.end ());
            if (join_result_ == ZLINK_ACTOR_JOIN_ACCEPTED) {
                after.spot_rid = route.join_target_spot_rid;
                after.spot_generation = route.join_target_spot_generation;
                after.membership_epoch += 1;
            }

            completion.actor.node_rid = rid_value (node->routing_id);
            snprintf (completion.actor.actor_id, sizeof (completion.actor.actor_id), "%s",
                      id.c_str ());
            completion.actor.generation = after.generation;
            completion.location.struct_size = sizeof (completion.location);
            completion.location.version = 1;
            completion.location.actor = completion.actor;
            completion.location.spot_rid = rid_value (after.spot_rid);
            completion.location.spot_generation = after.spot_generation;
            completion.location.membership_epoch = after.membership_epoch;

            std::unordered_map<uint64_t, pending_operation_t>::iterator op_it =
              node->operations.find (route.operation_id.low);
            if (op_it == node->operations.end ()) {
                route.consumed = true;
                return ZLINK_SUBMIT_OK;
            }
            op = op_it->second;
            join_timeout_task = op_it->second.timeout_task;
            completion_record->operation_id = op.id;
            completion_record->operation_kind = op.kind;
            completion_record->terminal_result =
              join_result_ == ZLINK_ACTOR_JOIN_ACCEPTED ? ZLINK_REQUEST_OK
                                                        : ZLINK_REQUEST_REJECTED;
            completion_record->failure_errno =
              join_result_ == ZLINK_ACTOR_JOIN_ACCEPTED ? 0 : EACCES;
            memcpy (&completion_record->kind_data[0], &completion, sizeof (completion));
            ready_owner = op.requester;

            std::map<owner_id_t, owner_state_t>::iterator owner_it =
              node->owners.find (op.requester);
            if (owner_it == node->owners.end ()) {
                node->operations.erase (op_it);
                route.consumed = true;
                consumed_without_owner = true;
            }
            if (!consumed_without_owner) {
            mailbox_t &mailbox = owner_it->second.domains[domain_infrastructure];
            const std::pair<owner_id_t, int> ready_key (
              op.requester, static_cast<int> (domain_infrastructure));
            const std::shared_ptr<completion_reservation_t> reservation =
              op_it->second.completion;
            if (!reservation || reservation->records.empty ()) {
                errno = EFAULT;
                return ZLINK_SUBMIT_INTERNAL_ERROR;
            }
            bool ready_inserted = false;
            try {
#ifdef ZLINK_BUILD_TESTS
                test_maybe_throw_alloc ();
#endif
                ready_inserted = node->ready.insert (ready_key).second;
            }
            catch (const std::bad_alloc &) {
                if (ready_inserted)
                    node->ready.erase (ready_key);
                route.in_flight = false;
                errno = ENOMEM;
                return ZLINK_SUBMIT_OUT_OF_MEMORY;
            }
            if (!reservation->prepared) {
                *reservation->records.front () = std::move (*completion_record);
                reservation->prepared = true;
            }
            const size_t completion_bytes =
              reservation->records.front ()->byte_size;
            mailbox.records.splice (mailbox.records.end (),
                                    reservation->records);

            if (join_result_ == ZLINK_ACTOR_JOIN_ACCEPTED) {
                std::map<std::string, spot_state_t>::iterator prev_it =
                  node->spots.find (prev_key);
                if (prev_it != node->spots.end ()
                    && prev_it->second.active_actor_count > 0) {
                    prev_it->second.active_actor_count -= 1;
                    maybe_end_spot_locked (node, prev_key);
                }
                actor = after;
                std::map<std::string, spot_state_t>::iterator new_it =
                  node->spots.find (new_key);
                if (new_it != node->spots.end ())
                    new_it->second.active_actor_count += 1;
            }
            mailbox.pending_messages += 1;
            mailbox.pending_bytes += completion_bytes;
            node->operations.erase (op_it);
            route.in_flight = false;
            route.consumed = true;
            deliver_local = true;
            }
        }
    }

    //  The operation was consumed on these two outcomes only; error returns
    //  above leave it pending, so its timeout task must stay armed.
    if (join_timeout_task && (consumed_without_owner || deliver_local))
        zlink::request_timeout::cancel (join_timeout_task);
    if (consumed_without_owner)
        return ZLINK_SUBMIT_OK;

    if (remote) {
        const zlink_submit_result_t rc =
          wire_submit_join_reply (node, remote_origin, remote_correlation, join_result_,
                                  remote_joined_spot, remote_joined_generation, parts_,
                                  part_count_, flags_);
        std::lock_guard<std::mutex> lock (node->mutex);
        std::unordered_map<uint64_t, reply_route_t>::iterator it =
          node->reply_routes.find (serial);
        if (it != node->reply_routes.end ()) {
            it->second.in_flight = false;
            if (rc == ZLINK_SUBMIT_OK) {
                if (join_result_ == ZLINK_ACTOR_JOIN_ACCEPTED) {
                    std::map<std::string, spot_state_t>::iterator spot_it =
                      node->spots.find (remote_spot_key);
                    if (spot_it != node->spots.end ()
                        && spot_it->second.generation == remote_joined_generation)
                        spot_it->second.active_actor_count += 1;
                }
                it->second.consumed = true;
            }
        }
        return rc;
    }
    if (deliver_local) {
        signal_ready (node, ready_owner, domain_infrastructure);
        observe_operation_completed (
          node, op.id,
          join_result_ == ZLINK_ACTOR_JOIN_ACCEPTED ? ZLINK_REQUEST_OK
                                                    : ZLINK_REQUEST_REJECTED,
          join_result_ == ZLINK_ACTOR_JOIN_ACCEPTED ? 0 : EACCES);
    }
    return ZLINK_SUBMIT_OK;
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
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
    mesh_node_pin_t node_pin (mesh_node_);
    mesh_node_t *node = node_pin.get ();
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
            operation_submission_t submission (
              node, true, ZLINK_MESH_OPERATION_ACTOR_REQUEST, requester, timeout_ms_);
            if (!submission.valid ())
                return ZLINK_SUBMIT_OUT_OF_MEMORY;
            const zlink_mesh_operation_id_t op_id = submission.operation_id ();
            const zlink_submit_result_t rc =
              wire_submit_actor_data (node, target_node, true, op_id.low, source_actor_,
                                      *target_actor_, metadata_, parts_, part_count_, flags_);
            if (rc != ZLINK_SUBMIT_OK)
                return rc;
            submission.commit ();
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
    try {
        record->kind = is_request ? ZLINK_MESH_RECORD_ACTOR_REQUEST : ZLINK_MESH_RECORD_ACTOR_SEND;
        record->source_node_rid = node->routing_id;
        if (source_actor_)
            record->source_actor = *source_actor_;
        if (metadata_) {
            record->has_metadata = true;
            record->application_metadata.assign (metadata_->data,
                                                 metadata_->data + metadata_->size);
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
    }
    catch (const std::bad_alloc &) {
        errno = ENOMEM;
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    }

    operation_submission_t submission (
      node, is_request, ZLINK_MESH_OPERATION_ACTOR_REQUEST, requester,
      is_request ? timeout_ms_ : 0);
    if (!submission.valid ())
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    const zlink_mesh_operation_id_t op_id = submission.operation_id ();
    uint64_t reply_serial = 0;
    if (is_request) {
        reply_route_t route;
        route.kind = reply_route_t::kind_generic;
        route.requester = requester;
        route.operation_kind = ZLINK_MESH_OPERATION_ACTOR_REQUEST;
        memset (&route.join_actor, 0, sizeof (route.join_actor));
        if (!submission.add_reply_route (route, &reply_serial))
            return ZLINK_SUBMIT_OUT_OF_MEMORY;
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
        switch (errno) {
            case EAGAIN:
            case ETIMEDOUT:
                return ZLINK_SUBMIT_BACKPRESSURED;
            case ESHUTDOWN:
                return ZLINK_SUBMIT_INVALID_STATE;
            case ENOMEM:
                return ZLINK_SUBMIT_OUT_OF_MEMORY;
            default:
                return ZLINK_SUBMIT_INTERNAL_ERROR;
        }
    }
    if (is_request) {
        submission.commit ();
        *operation_id_out_ = op_id;
    }
    return ZLINK_SUBMIT_OK;
}
}

zlink_submit_result_t zlink_mesh_node_send_to_actor (void *mesh_node_,
                                                     const zlink_actor_ref_t *actor_,
                                                     const zlink_mesh_metadata_view_t *actor_metadata_,
                                                     const zlink_msg_t *parts_,
                                                     size_t part_count_,
                                                     zlink_send_flags_t flags_)
try {
    return actor_submit (mesh_node_, NULL, actor_, actor_metadata_, parts_, part_count_, NULL,
                         flags_, 0);
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}

zlink_submit_result_t zlink_mesh_node_request_to_actor (void *mesh_node_,
                                                        const zlink_actor_ref_t *actor_,
                                                        const zlink_mesh_metadata_view_t *actor_metadata_,
                                                        const zlink_msg_t *parts_,
                                                        size_t part_count_,
                                                        zlink_mesh_operation_id_t *operation_id_out_,
                                                        zlink_send_flags_t flags_,
                                                        uint32_t timeout_ms_)
try {
    if (!operation_id_out_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    return actor_submit (mesh_node_, NULL, actor_, actor_metadata_, parts_, part_count_,
                         operation_id_out_, flags_, timeout_ms_);
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}

zlink_submit_result_t zlink_actor_send_to_actor (void *mesh_node_,
                                                 const zlink_actor_ref_t *source_actor_,
                                                 const zlink_actor_ref_t *target_actor_,
                                                 const zlink_mesh_metadata_view_t *actor_metadata_,
                                                 const zlink_msg_t *parts_,
                                                 size_t part_count_,
                                                 zlink_send_flags_t flags_)
try {
    if (!source_actor_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    return actor_submit (mesh_node_, source_actor_, target_actor_, actor_metadata_, parts_,
                         part_count_, NULL, flags_, 0);
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
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
try {
    if (!source_actor_ || !operation_id_out_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    return actor_submit (mesh_node_, source_actor_, target_actor_, actor_metadata_, parts_,
                         part_count_, operation_id_out_, flags_, timeout_ms_);
}
catch (const std::bad_alloc &) {
    return submit_out_of_memory_result ();
}
