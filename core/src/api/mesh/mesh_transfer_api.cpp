/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/mesh/mesh_c_internal.hpp"
#include "services/mesh/mesh_wire.hpp"

#include "utils/err.hpp"
#include "utils/macros.hpp"

#include <string.h>

using namespace zlink::mesh;

//  Actor transfer fence: token lifecycle, source fence/snapshot, target
//  staging and the automatic data plane between the two prepare tokens. The
//  framework location authority owns the durable decision; this file only
//  validates tokens and moves the frozen backlog.

namespace
{
const uint64_t transfer_token_marker = 0x5452464eu; //  'TRFN'

void seal_transfer_token (mesh_node_t *node_, uint64_t serial_, zlink_actor_transfer_token_t *out_)
{
    memset (out_, 0, sizeof (*out_));
    out_->opaque[0] = reinterpret_cast<uintptr_t> (node_);
    out_->opaque[1] = node_->lifecycle_generation;
    out_->opaque[2] = serial_;
    out_->opaque[3] = transfer_token_marker;
}

int unseal_transfer_token (const zlink_actor_transfer_token_t *token_,
                           mesh_node_t **node_out_,
                           uint64_t *serial_out_)
{
    if (!token_ || token_->opaque[3] != transfer_token_marker) {
        errno = ESTALE;
        return -1;
    }
    *node_out_ = reinterpret_cast<mesh_node_t *> (token_->opaque[0]);
    *serial_out_ = token_->opaque[2];
    return 0;
}

//  Snapshot of the fields a TRANSFER_CONTROL record reports.
struct transfer_control_view_t
{
    zlink_actor_transfer_role_t role;
    zlink_actor_transfer_id_t transfer_id;
    zlink_actor_ref_t actor;
    uint64_t membership_epoch;
    uint64_t final_sequence;
};

transfer_control_view_t control_view (const transfer_state_t &transfer_)
{
    transfer_control_view_t view;
    view.role = transfer_.role;
    view.transfer_id = transfer_.transfer_id;
    view.actor = transfer_.actor;
    view.membership_epoch =
      transfer_.committed_epoch != 0 ? transfer_.committed_epoch : transfer_.expected_epoch;
    view.final_sequence = transfer_.final_sequence;
    return view;
}

//  Emits the TRANSFER_CONTROL record into the actor's infrastructure claim.
void emit_transfer_control (mesh_node_t *node_,
                            const transfer_control_view_t &view_,
                            zlink_actor_transfer_phase_t phase_,
                            int32_t result_code_,
                            int32_t failure_errno_)
{
    std::unique_ptr<queued_record_t> record (new (std::nothrow) queued_record_t ());
    if (!record.get ())
        return;
    record->kind = ZLINK_MESH_RECORD_TRANSFER_CONTROL;
    record->source_node_rid = node_->routing_id;

    zlink_actor_transfer_control_t data;
    memset (&data, 0, sizeof (data));
    data.struct_size = sizeof (data);
    data.version = 1;
    data.phase = phase_;
    data.role = view_.role;
    data.transfer_id = view_.transfer_id;
    data.actor = view_.actor;
    data.membership_epoch = view_.membership_epoch;
    data.final_sequence = view_.final_sequence;
    data.result_code = result_code_;
    data.failure_errno = failure_errno_;
    record->kind_data.assign (reinterpret_cast<unsigned char *> (&data),
                              reinterpret_cast<unsigned char *> (&data) + sizeof (data));

    const owner_id_t owner = actor_owner (view_.actor.actor_id, view_.actor.generation);
    (void) admit_record (node_, owner, domain_infrastructure, record, false, 0);
}

transfer_state_t *find_transfer_locked (mesh_node_t *node_, uint64_t serial_)
{
    std::unordered_map<uint64_t, transfer_state_t>::iterator it = node_->transfers.find (serial_);
    return it == node_->transfers.end () ? NULL : &it->second;
}

transfer_state_t *find_transfer_by_id_locked (mesh_node_t *node_,
                                              const zlink_actor_transfer_id_t &id_)
{
    for (std::unordered_map<uint64_t, transfer_state_t>::iterator it = node_->transfers.begin ();
         it != node_->transfers.end (); ++it) {
        if (it->second.transfer_id.high == id_.high && it->second.transfer_id.low == id_.low)
            return &it->second;
    }
    return NULL;
}

bool peer_admitted_locked (mesh_node_t *node_, const rid_bytes_t &rid_)
{
    for (size_t i = 0; i < node_->peers.size (); ++i) {
        if (node_->peers[i].state == ZLINK_MESH_PEER_ADMITTED && node_->peers[i].rid == rid_)
            return true;
    }
    return false;
}

//  Sends the frozen snapshot range (acked_high_water, final_sequence] to the
//  target. Caller holds no locks; sequences are 1-based snapshot indices.
void send_snapshot_from (mesh_node_t *node_, uint64_t serial_)
{
    for (;;) {
        zlink_actor_transfer_id_t transfer_id;
        rid_bytes_t peer;
        const queued_record_t *record = NULL;
        uint64_t sequence = 0;
        uint64_t relay_serial = 0;
        {
            std::lock_guard<std::mutex> lock (node_->mutex);
            transfer_state_t *transfer = find_transfer_locked (node_, serial_);
            if (!transfer || transfer->role != ZLINK_ACTOR_TRANSFER_SOURCE
                || transfer->phase != ZLINK_ACTOR_TRANSFER_FENCED || !transfer->ready_exchanged)
                return;
            if (transfer->acked_high_water >= transfer->final_sequence)
                return;
            sequence = transfer->acked_high_water + 1;
            if (sequence > transfer->snapshot.size ())
                return;
            record = transfer->snapshot[sequence - 1].get ();
            transfer_id = transfer->transfer_id;
            peer = transfer->peer_node_rid;
            //  Transferred requests keep replying through the source route:
            //  the record's token stays sealed here, and the target reseals
            //  its own token against this relay serial.
            if (record->has_reply_token) {
                mesh_node_t *token_node = NULL;
                uint64_t reply_serial = 0;
                zlink_mesh_reply_token_t token = record->reply_token;
                if (unseal_reply_token (&token, &token_node, &reply_serial) == 0
                    && token_node == node_)
                    relay_serial = reply_serial;
            }
        }
        if (wire_submit_transfer_data (node_, peer, transfer_id, sequence, *record, relay_serial)
            != ZLINK_SUBMIT_OK)
            return; //  Retried on the next ACK or ready exchange.
        //  Optimistically continue; the ACK moves acked_high_water. To keep
        //  the loop bounded we send one record per ACK round-trip.
        return;
    }
}
} // namespace

namespace zlink
{
namespace mesh
{
void transfer_handle_ready (mesh_node_t *node_,
                            const rid_bytes_t &source_rid_,
                            const zlink_actor_transfer_id_t &transfer_id_,
                            const zlink_actor_ref_t &actor_,
                            uint64_t expected_epoch_,
                            uint64_t final_sequence_,
                            uint8_t role_)
{
    LIBZLINK_UNUSED (actor_);
    LIBZLINK_UNUSED (expected_epoch_);
    LIBZLINK_UNUSED (final_sequence_);
    uint64_t serial = 0;
    bool confirm = false;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        transfer_state_t *transfer = find_transfer_by_id_locked (node_, transfer_id_);
        if (!transfer || transfer->peer_node_rid != source_rid_)
            return;
        transfer->ready_exchanged = true;
        serial = transfer->serial;
        //  The target initiates; the source confirms and starts the data
        //  plane. role_ names the sender's role.
        confirm = transfer->role == ZLINK_ACTOR_TRANSFER_SOURCE
                  && role_ == ZLINK_ACTOR_TRANSFER_TARGET;
        node_->cv.notify_all ();
    }
    if (confirm) {
        zlink_actor_ref_t actor;
        uint64_t expected = 0;
        uint64_t final_sequence = 0;
        rid_bytes_t peer;
        {
            std::lock_guard<std::mutex> lock (node_->mutex);
            transfer_state_t *transfer = find_transfer_locked (node_, serial);
            if (!transfer)
                return;
            actor = transfer->actor;
            expected = transfer->expected_epoch;
            final_sequence = transfer->final_sequence;
            peer = transfer->peer_node_rid;
        }
        wire_submit_transfer_ready (node_, peer, transfer_id_, actor, expected, final_sequence,
                                    ZLINK_ACTOR_TRANSFER_SOURCE);
        send_snapshot_from (node_, serial);
    }
}

void transfer_handle_data (mesh_node_t *node_,
                           const rid_bytes_t &source_rid_,
                           const zlink_actor_transfer_id_t &transfer_id_,
                           uint64_t sequence_,
                           uint64_t relay_serial_,
                           std::unique_ptr<queued_record_t> *record_)
{
    zlink_actor_transfer_id_t transfer_id = transfer_id_;
    rid_bytes_t peer;
    uint64_t ack = 0;
    bool send_ack = false;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        transfer_state_t *transfer = find_transfer_by_id_locked (node_, transfer_id_);
        if (!transfer || transfer->role != ZLINK_ACTOR_TRANSFER_TARGET
            || transfer->peer_node_rid != source_rid_
            || (transfer->phase != ZLINK_ACTOR_TRANSFER_PREPARING
                && transfer->phase != ZLINK_ACTOR_TRANSFER_COMMITTED))
            return;
        if (sequence_ == 0 || sequence_ > transfer->final_sequence)
            return;
        //  Same-key retransmissions stage once; a different payload for the
        //  same key is a protocol failure surfaced as a control record.
        std::map<uint64_t, std::unique_ptr<queued_record_t>>::iterator staged =
          transfer->staged.find (sequence_);
        if (staged == transfer->staged.end ()) {
            //  Reseal the reply route against this node so the transferred
            //  request replies through the source relay.
            if (relay_serial_ != 0) {
                const uint64_t reply_serial = node_->next_reply_serial++;
                reply_route_t route;
                route.requester = node_owner ();
                route.requester_node_generation = node_->lifecycle_generation;
                route.operation_kind = (*record_)->operation_kind;
                route.remote_origin = true;
                route.origin_rid = source_rid_;
                route.origin_generation = 0;
                route.origin_correlation = relay_serial_;
                node_->reply_routes[reply_serial] = route;
                (*record_)->has_reply_token = true;
                seal_reply_token (node_, reply_serial, &(*record_)->reply_token);
            }
            transfer->staged[sequence_] = std::move (*record_);
        }
        //  Contiguous staged high-water.
        uint64_t high = 0;
        while (transfer->staged.count (high + 1))
            ++high;
        transfer->acked_high_water = high;
        ack = high;
        peer = transfer->peer_node_rid;
        send_ack = true;
        node_->cv.notify_all ();
    }
    if (send_ack)
        wire_submit_transfer_ack (node_, peer, transfer_id, ack);
}

void transfer_handle_ack (mesh_node_t *node_,
                          const rid_bytes_t &source_rid_,
                          const zlink_actor_transfer_id_t &transfer_id_,
                          uint64_t high_water_)
{
    uint64_t serial = 0;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        transfer_state_t *transfer = find_transfer_by_id_locked (node_, transfer_id_);
        if (!transfer || transfer->role != ZLINK_ACTOR_TRANSFER_SOURCE
            || transfer->peer_node_rid != source_rid_)
            return;
        if (high_water_ > transfer->acked_high_water)
            transfer->acked_high_water = high_water_;
        serial = transfer->serial;
        node_->cv.notify_all ();
    }
    send_snapshot_from (node_, serial);
}

void transfer_handle_reply_relay (mesh_node_t *node_,
                                  const rid_bytes_t &source_rid_,
                                  uint64_t relay_serial_,
                                  std::vector<zlink_msg_t> *parts_)
{
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        if (!peer_admitted_locked (node_, source_rid_))
            return;
    }
    (void) deliver_reply_via_route (node_, relay_serial_, parts_);
}
}
}

//  --- public API ------------------------------------------------------------------

zlink_request_result_t
zlink_mesh_node_actor_transfer_prepare (void *mesh_node_,
                                        const zlink_actor_transfer_prepare_t *prepare_,
                                        uint32_t timeout_ms_,
                                        zlink_actor_transfer_token_t *token_out_,
                                        zlink_actor_transfer_prepare_result_t *result_out_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    if (check_versioned (prepare_) != 0 || !token_out_ || check_versioned (result_out_) != 0)
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    memset (token_out_, 0, sizeof (*token_out_));
    if (prepare_->role != ZLINK_ACTOR_TRANSFER_SOURCE
        && prepare_->role != ZLINK_ACTOR_TRANSFER_TARGET) {
        errno = EINVAL;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    const size_t id_len = strnlen (prepare_->actor.actor_id, sizeof (prepare_->actor.actor_id));
    if (id_len == 0 || id_len > ZLINK_ACTOR_ID_MAX || prepare_->actor.generation == 0
        || prepare_->peer_node_rid.size == 0) {
        errno = EINVAL;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    if ((prepare_->transfer_id.high | prepare_->transfer_id.low) == 0) {
        errno = EINVAL;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }

    const rid_bytes_t peer = rid_bytes (prepare_->peer_node_rid);
    const std::string actor_id (prepare_->actor.actor_id);
    const uint64_t deadline = timeout_ms_ > 0 ? now_ms () + timeout_ms_ : 0;

    uint64_t serial = 0;
    if (prepare_->role == ZLINK_ACTOR_TRANSFER_SOURCE) {
        std::unique_lock<std::mutex> lock (node->mutex);
        if (node->state == ZLINK_MESH_NODE_DRAINING || node->state == ZLINK_MESH_NODE_STOPPED) {
            errno = ESHUTDOWN;
            return ZLINK_REQUEST_INVALID_STATE;
        }
        std::map<std::string, actor_state_t>::iterator it = node->actors.find (actor_id);
        if (it == node->actors.end () || it->second.generation != prepare_->actor.generation) {
            errno = ESTALE;
            return ZLINK_REQUEST_INVALID_STATE;
        }
        actor_state_t &actor = it->second;
        if (actor.membership_epoch != prepare_->expected_membership_epoch) {
            errno = ESTALE;
            return ZLINK_REQUEST_INVALID_STATE;
        }
        if (node->active_transfer_by_actor.count (actor_id)) {
            errno = EBUSY;
            return ZLINK_REQUEST_INVALID_STATE;
        }
        if (!peer_admitted_locked (node, peer)) {
            errno = ENOTCONN;
            return ZLINK_REQUEST_NOT_CONNECTED;
        }

        //  The fence waits for the active application claim to release.
        const owner_id_t owner = actor_owner (actor_id, actor.generation);
        std::map<owner_id_t, owner_state_t>::iterator owner_it = node->owners.find (owner);
        if (owner_it == node->owners.end ()) {
            errno = ESTALE;
            return ZLINK_REQUEST_INVALID_STATE;
        }
        while (owner_it->second.domains[domain_application].claimed) {
            if (timeout_ms_ == 0 || now_ms () >= deadline) {
                errno = ETIMEDOUT;
                return ZLINK_REQUEST_TIMED_OUT;
            }
            node->cv.wait_for (lock, std::chrono::milliseconds (deadline - now_ms ()));
            owner_it = node->owners.find (owner);
            if (owner_it == node->owners.end ()) {
                errno = ESTALE;
                return ZLINK_REQUEST_INVALID_STATE;
            }
        }

        serial = node->next_transfer_serial++;
        transfer_state_t &transfer = node->transfers[serial];
        transfer.serial = serial;
        transfer.role = ZLINK_ACTOR_TRANSFER_SOURCE;
        transfer.transfer_id = prepare_->transfer_id;
        transfer.actor = prepare_->actor;
        transfer.actor.node_rid = rid_value (node->routing_id);
        transfer.expected_epoch = prepare_->expected_membership_epoch;
        transfer.node_generation = node->lifecycle_generation;
        transfer.peer_node_rid = peer;
        transfer.phase = ZLINK_ACTOR_TRANSFER_FENCED;

        //  Freeze: the application mailbox becomes the snapshot in original
        //  FIFO order; new application traffic is fenced to EAGAIN.
        mailbox_t &mailbox = owner_it->second.domains[domain_application];
        while (!mailbox.records.empty ()) {
            transfer.reserve_bytes += mailbox.records.front ()->byte_size;
            transfer.snapshot.push_back (std::move (mailbox.records.front ()));
            mailbox.records.pop_front ();
        }
        mailbox.pending_messages = 0;
        mailbox.pending_bytes = 0;
        node->ready.erase (std::make_pair (owner, static_cast<int> (domain_application)));
        owner_it->second.fenced_transfer_serial = serial;
        transfer.final_sequence = transfer.snapshot.size ();
        transfer.reserve_messages = transfer.snapshot.size ();
        node->active_transfer_by_actor[actor_id] = serial;

        init_versioned (result_out_);
        result_out_->role = ZLINK_ACTOR_TRANSFER_SOURCE;
        result_out_->transfer_id = transfer.transfer_id;
        result_out_->actor = transfer.actor;
        result_out_->final_sequence = transfer.final_sequence;
        result_out_->reserve_message_count = transfer.reserve_messages;
        result_out_->reserve_byte_count = transfer.reserve_bytes;
        seal_transfer_token (node, serial, token_out_);

        const transfer_control_view_t view = control_view (transfer);
        lock.unlock ();
        emit_transfer_control (node, view, ZLINK_ACTOR_TRANSFER_FENCED, ZLINK_REQUEST_OK, 0);
        return ZLINK_REQUEST_OK;
    }

    //  Target role: reserve capacity, install the placeholder actor and
    //  exchange readiness with the source before returning the token.
    {
        std::unique_lock<std::mutex> lock (node->mutex);
        if (node->state == ZLINK_MESH_NODE_DRAINING || node->state == ZLINK_MESH_NODE_STOPPED) {
            errno = ESHUTDOWN;
            return ZLINK_REQUEST_INVALID_STATE;
        }
        if (!peer_admitted_locked (node, peer)) {
            errno = ENOTCONN;
            return ZLINK_REQUEST_NOT_CONNECTED;
        }
        if (node->active_transfer_by_actor.count (actor_id)) {
            errno = EBUSY;
            return ZLINK_REQUEST_INVALID_STATE;
        }
        if (node->actors.count (actor_id)) {
            //  An active same-id actor on the target conflicts.
            errno = EEXIST;
            return ZLINK_REQUEST_CONFLICT;
        }
        //  Reserve frozen backlog capacity against the mailbox budgets.
        if (prepare_->reserve_message_count > node->effective_message_budget ()
            || prepare_->reserve_byte_count > node->effective_byte_budget ()) {
            errno = ENOBUFS;
            return ZLINK_REQUEST_BACKPRESSURED;
        }

        serial = node->next_transfer_serial++;
        transfer_state_t &transfer = node->transfers[serial];
        transfer.serial = serial;
        transfer.role = ZLINK_ACTOR_TRANSFER_TARGET;
        transfer.transfer_id = prepare_->transfer_id;
        transfer.actor = prepare_->actor;
        transfer.actor.node_rid = rid_value (node->routing_id);
        transfer.expected_epoch = prepare_->expected_membership_epoch;
        transfer.node_generation = node->lifecycle_generation;
        transfer.peer_node_rid = peer;
        transfer.phase = ZLINK_ACTOR_TRANSFER_PREPARING;
        transfer.final_sequence = prepare_->final_sequence;
        transfer.reserve_messages = prepare_->reserve_message_count;
        transfer.reserve_bytes = prepare_->reserve_byte_count;
        transfer.deadline_ms = deadline;
        node->active_transfer_by_actor[actor_id] = serial;

        //  Placeholder actor: owns the staged mailbox and the transfer
        //  control lane; application dispatch starts only at activate.
        actor_state_t &actor = node->actors[actor_id];
        actor.id = actor_id;
        actor.generation = prepare_->actor.generation;
        actor.membership_epoch = prepare_->expected_membership_epoch;
        actor.draining = true;
        owner_state_t &owner = node->owners[actor_owner (actor_id, actor.generation)];
        owner.id = actor_owner (actor_id, actor.generation);
        owner.actor = transfer.actor;
    }

    //  Readiness exchange outside the lock: target announces, source
    //  confirms and starts sending the snapshot.
    wire_submit_transfer_ready (node, peer, prepare_->transfer_id, prepare_->actor,
                                prepare_->expected_membership_epoch, prepare_->final_sequence,
                                ZLINK_ACTOR_TRANSFER_TARGET);
    {
        std::unique_lock<std::mutex> lock (node->mutex);
        transfer_state_t *transfer = find_transfer_locked (node, serial);
        while (transfer && !transfer->ready_exchanged) {
            if (timeout_ms_ == 0 || now_ms () >= deadline) {
                //  No token on failure: unwind the reservation.
                node->active_transfer_by_actor.erase (actor_id);
                node->owners.erase (actor_owner (actor_id, prepare_->actor.generation));
                node->actors.erase (actor_id);
                node->transfers.erase (serial);
                errno = ETIMEDOUT;
                return ZLINK_REQUEST_TIMED_OUT;
            }
            node->cv.wait_for (lock, std::chrono::milliseconds (
                                       std::min<uint64_t> (50, deadline - now_ms ())));
            transfer = find_transfer_locked (node, serial);
        }
        if (!transfer) {
            errno = ESTALE;
            return ZLINK_REQUEST_INVALID_STATE;
        }

        init_versioned (result_out_);
        result_out_->role = ZLINK_ACTOR_TRANSFER_TARGET;
        result_out_->transfer_id = transfer->transfer_id;
        result_out_->actor = transfer->actor;
        result_out_->final_sequence = transfer->final_sequence;
        result_out_->reserve_message_count = transfer->reserve_messages;
        result_out_->reserve_byte_count = transfer->reserve_bytes;
        seal_transfer_token (node, serial, token_out_);

        const transfer_control_view_t view = control_view (*transfer);
        lock.unlock ();
        emit_transfer_control (node, view, ZLINK_ACTOR_TRANSFER_PREPARING, ZLINK_REQUEST_OK, 0);
    }
    return ZLINK_REQUEST_OK;
}

zlink_config_result_t
zlink_mesh_node_actor_transfer_commit (const zlink_actor_transfer_token_t *token_,
                                       uint64_t new_membership_epoch_)
{
    mesh_node_t *node_ptr = NULL;
    uint64_t serial = 0;
    if (unseal_transfer_token (token_, &node_ptr, &serial) != 0)
        return ZLINK_CONFIG_INVALID_STATE;
    mesh_node_t *node = as_mesh_node (node_ptr);
    if (!node) {
        errno = ESTALE;
        return ZLINK_CONFIG_INVALID_STATE;
    }

    bool source_commit = false;
    transfer_control_view_t control_copy;
    memset (&control_copy, 0, sizeof (control_copy));
    {
        std::unique_lock<std::mutex> lock (node->mutex);
        transfer_state_t *transfer = find_transfer_locked (node, serial);
        if (!transfer || transfer->node_generation != node->lifecycle_generation) {
            errno = ESTALE;
            return ZLINK_CONFIG_INVALID_STATE;
        }
        if (transfer->phase == ZLINK_ACTOR_TRANSFER_ABORTED) {
            errno = EALREADY;
            return ZLINK_CONFIG_INVALID_STATE;
        }
        //  Idempotent retry of a completed commit with the same epoch.
        if (transfer->committed_epoch != 0) {
            if (transfer->committed_epoch == new_membership_epoch_
                && (transfer->phase == ZLINK_ACTOR_TRANSFER_COMMITTED
                    || transfer->phase == ZLINK_ACTOR_TRANSFER_ACTIVATED))
                return ZLINK_CONFIG_OK;
            errno = EALREADY;
            return ZLINK_CONFIG_INVALID_STATE;
        }
        if (new_membership_epoch_ != transfer->expected_epoch + 1) {
            errno = ESTALE;
            return ZLINK_CONFIG_INVALID_STATE;
        }

        if (transfer->role == ZLINK_ACTOR_TRANSFER_TARGET) {
            if (transfer->phase != ZLINK_ACTOR_TRANSFER_PREPARING) {
                errno = EALREADY;
                return ZLINK_CONFIG_INVALID_STATE;
            }
            //  All frozen records must be staged contiguously first; the
            //  data plane keeps running, so wait inside the sealed deadline.
            while (transfer->acked_high_water < transfer->final_sequence) {
                if (!peer_admitted_locked (node, transfer->peer_node_rid)) {
                    errno = ENOTCONN;
                    return ZLINK_CONFIG_INVALID_STATE;
                }
                if (transfer->deadline_ms != 0 && now_ms () >= transfer->deadline_ms) {
                    errno = ETIMEDOUT;
                    return ZLINK_CONFIG_INVALID_STATE;
                }
                node->cv.wait_for (lock, std::chrono::milliseconds (50));
                transfer = find_transfer_locked (node, serial);
                if (!transfer) {
                    errno = ESTALE;
                    return ZLINK_CONFIG_INVALID_STATE;
                }
            }
            transfer->committed_epoch = new_membership_epoch_;
            transfer->phase = ZLINK_ACTOR_TRANSFER_COMMITTED;
            const std::string id (transfer->actor.actor_id);
            std::map<std::string, actor_state_t>::iterator it = node->actors.find (id);
            if (it != node->actors.end ())
                it->second.membership_epoch = new_membership_epoch_;
            control_copy = control_view (*transfer);
        } else {
            if (transfer->phase != ZLINK_ACTOR_TRANSFER_FENCED) {
                errno = EALREADY;
                return ZLINK_CONFIG_INVALID_STATE;
            }
            //  Source commit: the framework confirmed target activation.
            //  Remove the old route/admission and release the snapshot.
            source_commit = true;
            transfer->committed_epoch = new_membership_epoch_;
            transfer->phase = ZLINK_ACTOR_TRANSFER_COMMITTED;
            transfer->snapshot.clear ();
            const std::string id (transfer->actor.actor_id);
            std::map<std::string, actor_state_t>::iterator it = node->actors.find (id);
            if (it != node->actors.end () && it->second.generation == transfer->actor.generation) {
                const std::string spot_key (it->second.spot_rid.begin (),
                                            it->second.spot_rid.end ());
                std::map<std::string, spot_state_t>::iterator spot_it =
                  node->spots.find (spot_key);
                if (it->second.spot_node_rid.empty () && spot_it != node->spots.end ()
                    && spot_it->second.active_actor_count > 0)
                    spot_it->second.active_actor_count -= 1;
                node->actors.erase (it);
            }
            //  The owner mailbox stays for the framework to drain the
            //  terminal control record; the fence stays engaged.
            node->active_transfer_by_actor.erase (id);
            control_copy = control_view (*transfer);
        }
    }
    emit_transfer_control (node, control_copy, ZLINK_ACTOR_TRANSFER_COMMITTED, ZLINK_REQUEST_OK,
                           0);
    LIBZLINK_UNUSED (source_commit);
    return ZLINK_CONFIG_OK;
}

zlink_config_result_t
zlink_mesh_node_actor_transfer_activate (const zlink_actor_transfer_token_t *token_)
{
    mesh_node_t *node_ptr = NULL;
    uint64_t serial = 0;
    if (unseal_transfer_token (token_, &node_ptr, &serial) != 0)
        return ZLINK_CONFIG_INVALID_STATE;
    mesh_node_t *node = as_mesh_node (node_ptr);
    if (!node) {
        errno = ESTALE;
        return ZLINK_CONFIG_INVALID_STATE;
    }

    transfer_control_view_t control_copy;
    memset (&control_copy, 0, sizeof (control_copy));
    owner_id_t ready_owner;
    bool publish_ready = false;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        transfer_state_t *transfer = find_transfer_locked (node, serial);
        if (!transfer || transfer->node_generation != node->lifecycle_generation) {
            errno = ESTALE;
            return ZLINK_CONFIG_INVALID_STATE;
        }
        if (transfer->phase == ZLINK_ACTOR_TRANSFER_ACTIVATED)
            return ZLINK_CONFIG_OK; //  Idempotent retry.
        if (transfer->role != ZLINK_ACTOR_TRANSFER_TARGET
            || transfer->phase != ZLINK_ACTOR_TRANSFER_COMMITTED) {
            errno = transfer->phase == ZLINK_ACTOR_TRANSFER_ABORTED ? EALREADY : EINVAL;
            return ZLINK_CONFIG_INVALID_STATE;
        }

        //  Publish: staged records become the actor's application mailbox in
        //  sequence order and the actor leaves the placeholder drain state.
        const std::string id (transfer->actor.actor_id);
        std::map<std::string, actor_state_t>::iterator actor_it = node->actors.find (id);
        if (actor_it == node->actors.end ()) {
            errno = ESTALE;
            return ZLINK_CONFIG_INVALID_STATE;
        }
        actor_it->second.draining = false;
        ready_owner = actor_owner (id, actor_it->second.generation);
        std::map<owner_id_t, owner_state_t>::iterator owner_it = node->owners.find (ready_owner);
        if (owner_it != node->owners.end ()) {
            mailbox_t &mailbox = owner_it->second.domains[domain_application];
            for (std::map<uint64_t, std::unique_ptr<queued_record_t>>::iterator staged =
                   transfer->staged.begin ();
                 staged != transfer->staged.end (); ++staged) {
                mailbox.pending_messages += 1;
                mailbox.pending_bytes += staged->second->byte_size;
                mailbox.records.push_back (std::move (staged->second));
            }
            transfer->staged.clear ();
            if (!mailbox.records.empty ()) {
                node->ready.insert (
                  std::make_pair (ready_owner, static_cast<int> (domain_application)));
                publish_ready = true;
            }
        }
        transfer->phase = ZLINK_ACTOR_TRANSFER_ACTIVATED;
        node->active_transfer_by_actor.erase (id);
        node->cv.notify_all ();
        control_copy = control_view (*transfer);
    }
    if (publish_ready)
        signal_ready (node, ready_owner, domain_application);
    emit_transfer_control (node, control_copy, ZLINK_ACTOR_TRANSFER_ACTIVATED, ZLINK_REQUEST_OK,
                           0);
    return ZLINK_CONFIG_OK;
}

zlink_config_result_t
zlink_mesh_node_actor_transfer_abort (const zlink_actor_transfer_token_t *token_)
{
    mesh_node_t *node_ptr = NULL;
    uint64_t serial = 0;
    if (unseal_transfer_token (token_, &node_ptr, &serial) != 0)
        return ZLINK_CONFIG_INVALID_STATE;
    mesh_node_t *node = as_mesh_node (node_ptr);
    if (!node) {
        errno = ESTALE;
        return ZLINK_CONFIG_INVALID_STATE;
    }

    transfer_control_view_t control_copy;
    memset (&control_copy, 0, sizeof (control_copy));
    owner_id_t ready_owner;
    bool publish_ready = false;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        transfer_state_t *transfer = find_transfer_locked (node, serial);
        if (!transfer || transfer->node_generation != node->lifecycle_generation) {
            errno = ESTALE;
            return ZLINK_CONFIG_INVALID_STATE;
        }
        if (transfer->phase == ZLINK_ACTOR_TRANSFER_ABORTED)
            return ZLINK_CONFIG_OK; //  Idempotent retry.
        if (transfer->phase == ZLINK_ACTOR_TRANSFER_ACTIVATED
            || (transfer->role == ZLINK_ACTOR_TRANSFER_SOURCE
                && transfer->phase == ZLINK_ACTOR_TRANSFER_COMMITTED)) {
            errno = EALREADY;
            return ZLINK_CONFIG_INVALID_STATE;
        }

        const std::string id (transfer->actor.actor_id);
        if (transfer->role == ZLINK_ACTOR_TRANSFER_SOURCE) {
            //  Restore: the frozen snapshot returns to the mailbox in order
            //  and the fence lifts.
            const owner_id_t owner = actor_owner (id, transfer->actor.generation);
            std::map<owner_id_t, owner_state_t>::iterator owner_it = node->owners.find (owner);
            if (owner_it != node->owners.end ()) {
                mailbox_t &mailbox = owner_it->second.domains[domain_application];
                for (size_t i = transfer->snapshot.size (); i > 0; --i)
                    mailbox.records.push_front (std::move (transfer->snapshot[i - 1]));
                transfer->snapshot.clear ();
                mailbox.pending_messages = mailbox.records.size ();
                mailbox.pending_bytes = 0;
                for (std::deque<std::unique_ptr<queued_record_t>>::iterator record =
                       mailbox.records.begin ();
                     record != mailbox.records.end (); ++record)
                    mailbox.pending_bytes += (*record)->byte_size;
                owner_it->second.fenced_transfer_serial = 0;
                if (!mailbox.records.empty ()) {
                    ready_owner = owner;
                    node->ready.insert (
                      std::make_pair (owner, static_cast<int> (domain_application)));
                    publish_ready = true;
                }
            }
        } else {
            //  Target abort discards staged copies and the placeholder.
            transfer->staged.clear ();
            std::map<std::string, actor_state_t>::iterator actor_it = node->actors.find (id);
            if (actor_it != node->actors.end ()
                && actor_it->second.generation == transfer->actor.generation
                && actor_it->second.draining)
                node->actors.erase (actor_it);
        }
        transfer->phase = ZLINK_ACTOR_TRANSFER_ABORTED;
        node->active_transfer_by_actor.erase (id);
        node->cv.notify_all ();
        control_copy = control_view (*transfer);
    }
    if (publish_ready)
        signal_ready (node, ready_owner, domain_application);
    emit_transfer_control (node, control_copy, ZLINK_ACTOR_TRANSFER_ABORTED, ZLINK_REQUEST_OK, 0);
    return ZLINK_CONFIG_OK;
}
