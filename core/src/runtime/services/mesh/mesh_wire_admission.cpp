/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <string.h>

#include <memory>
#include <string>
#include <vector>

#include "services/mesh/mesh_wire_internal.hpp"
#include "api/mesh/mesh_c_internal.hpp"
#include "api/socket/socket_api_internal.hpp"
#include "utils/macros.hpp"

//  Peer admission state machine: HELLO/ADMIT/REJECT/UPDATE handling,
//  descriptor application, generation replacement and peer transitions.
namespace zlink
{
namespace mesh
{
//  --- peer bookkeeping ----------------------------------------------------------

peer_state_t *find_peer_by_rid_locked (mesh_node_t *node_, const rid_bytes_t &rid_)
{
    //  After a generation replacement a DRAINING predecessor shares the RID
    //  with the admitted successor; the admitted lifetime wins the lookup.
    peer_state_t *fallback = NULL;
    for (size_t i = 0; i < node_->peers.size (); ++i) {
        if (node_->peers[i].rid != rid_ || node_->peers[i].state == ZLINK_MESH_PEER_CLOSED)
            continue;
        if (node_->peers[i].state == ZLINK_MESH_PEER_ADMITTED)
            return &node_->peers[i];
        if (!fallback)
            fallback = &node_->peers[i];
    }
    return fallback;
}

peer_state_t *find_intent_by_endpoint_locked (mesh_node_t *node_, const std::string &endpoint_)
{
    //  A re-established transport re-runs the handshake, so intents in the
    //  ERROR or ADMITTED state are eligible again alongside CONNECTING.
    for (size_t i = 0; i < node_->peers.size (); ++i) {
        peer_state_t &peer = node_->peers[i];
        if (peer.endpoint != endpoint_ || peer.inbound)
            continue;
        if (peer.state == ZLINK_MESH_PEER_CONNECTING || peer.state == ZLINK_MESH_PEER_ERROR
            || peer.state == ZLINK_MESH_PEER_ADMITTED)
            return &peer;
    }
    return NULL;
}

void emit_peer_event (mesh_node_t *node_,
                      zlink_mesh_monitor_event_kind_t kind_,
                      const rid_bytes_t &rid_,
                      int32_t error_)
{
    zlink_mesh_monitor_event_t event;
    memset (&event, 0, sizeof (event));
    event.kind = kind_;
    if (!rid_.empty ()) {
        event.peer_rid.size = static_cast<uint8_t> (rid_.size ());
        memcpy (event.peer_rid.data, rid_.data (), rid_.size ());
    }
    event.failure_errno = error_;
    if (kind_ == ZLINK_MESH_MONITOR_PEER_REJECTED)
        event.result_code = error_ == EACCES ? ZLINK_CONNECT_AUTH_FAILED : ZLINK_CONNECT_CONFLICT;
    emit_monitor_event (node_, event);
}

//  Applies an admitted descriptor to a peer entry (mutex held).
void apply_descriptor_locked (peer_state_t *peer_, const wire_descriptor_t &descriptor_)
{
    //  Same-generation updates only move forward; a higher revision replaces
    //  the channel index atomically under the node mutex.
    if (peer_->descriptor_revision >= descriptor_.descriptor_revision
        && peer_->lifecycle_generation == descriptor_.lifecycle_generation)
        return;
    peer_->lifecycle_generation = descriptor_.lifecycle_generation;
    peer_->descriptor_revision = descriptor_.descriptor_revision;
    peer_->channels.clear ();
    for (size_t i = 0; i < descriptor_.channels.size (); ++i)
        peer_->channels[descriptor_.channels[i].first] = descriptor_.channels[i].second;
    peer_->last_changed_ms = now_ms ();
}

//  Validates an inbound descriptor against local admission rules. Returns 0
//  or -1 with errno describing the conflict class.
int validate_admission_locked (mesh_node_t *node_,
                               const rid_bytes_t &source_rid_,
                               const wire_descriptor_t &descriptor_,
                               const peer_state_t *intent_)
{
    if (descriptor_.mesh_name != node_->mesh_name) {
        errno = EEXIST;
        return -1;
    }
    if (descriptor_.trust_profile != node_->trust_profile) {
        errno = EACCES;
        return -1;
    }
    if (intent_ && intent_->has_expected_rid) {
        const rid_bytes_t expected = intent_->expected_rid;
        if (expected != source_rid_) {
            errno = ESTALE;
            return -1;
        }
    }
    const peer_state_t *existing = find_peer_by_rid_locked (node_, source_rid_);
    if (existing && existing->state == ZLINK_MESH_PEER_ADMITTED
        && existing->lifecycle_generation > descriptor_.lifecycle_generation) {
        //  A stale generation cannot replace a newer admitted lifetime.
        errno = ESTALE;
        return -1;
    }
    return 0;
}

//  --- ingress: control ------------------------------------------------------------

void handle_hello_or_admit (mesh_node_t *node_,
                            const rid_bytes_t &source_rid_,
                            const wire_descriptor_t &descriptor_,
                            bool is_hello_)
{
    bool send_admit = false;
    bool send_reject = false;
    int reject_errno = 0;
    bool admit_rejected = false;
    int admit_reject_errno = 0;
    bool generation_drained = false;
    zlink_routing_id_t target = rid_value (source_rid_);

    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        if (node_->state != ZLINK_MESH_NODE_STARTED && node_->state != ZLINK_MESH_NODE_PARTIAL_READY
            && node_->state != ZLINK_MESH_NODE_READY)
            return;

        peer_state_t *intent = NULL;
        if (!is_hello_) {
            //  ADMIT answers our HELLO: it must match a connecting (or
            //  re-connecting) intent by the advertised rid, or any
            //  connecting intent still awaiting its rid.
            intent = find_peer_by_rid_locked (node_, source_rid_);
            if (intent
                && (intent->inbound
                    || (intent->state != ZLINK_MESH_PEER_CONNECTING
                        && intent->state != ZLINK_MESH_PEER_ERROR
                        && intent->state != ZLINK_MESH_PEER_ADMITTED)))
                intent = NULL;
            if (!intent) {
                for (size_t i = 0; i < node_->peers.size () && !intent; ++i) {
                    if (node_->peers[i].state == ZLINK_MESH_PEER_CONNECTING
                        && node_->peers[i].rid.empty () && !node_->peers[i].inbound)
                        intent = &node_->peers[i];
                }
            }
            if (!intent)
                return;
        }

        if (validate_admission_locked (node_, source_rid_, descriptor_, intent) != 0) {
            const int reason = errno;
            if (is_hello_) {
                send_reject = true;
                reject_errno = reason;
            } else if (intent) {
                intent->state = ZLINK_MESH_PEER_ERROR;
                intent->last_error = reason;
                intent->last_changed_ms = now_ms ();
                recompute_readiness_locked (node_);
                admit_rejected = true;
                admit_reject_errno = reason;
            }
        } else {
            peer_state_t *peer = intent;
            if (!peer)
                peer = find_peer_by_rid_locked (node_, source_rid_);
            //  A higher lifecycle generation starts a separate lifetime: the
            //  previous admitted entry stays observable as DRAINING (excluded
            //  from new snapshots, closed with the transport or an explicit
            //  disconnect) and a fresh entry carries the new generation.
            if (peer && peer->state == ZLINK_MESH_PEER_ADMITTED
                && descriptor_.lifecycle_generation > peer->lifecycle_generation) {
                generation_drained = true;
                peer->state = ZLINK_MESH_PEER_DRAINING;
                peer->last_changed_ms = now_ms ();
                peer_state_t successor;
                successor.intent_id = node_->next_intent_id++;
                successor.source = peer->source;
                successor.inbound = peer->inbound;
                successor.endpoint = peer->endpoint;
                node_->peers.push_back (successor);
                peer = &node_->peers.back ();
            }
            if (!peer) {
                //  Inbound admitted peer without a local intent: the wire
                //  observed it rather than the operator configuring it, so
                //  its source is DISCOVERY (a later manual intent for the
                //  same endpoint merges into MIXED).
                peer_state_t fresh;
                fresh.intent_id = node_->next_intent_id++;
                fresh.source = ZLINK_MESH_PEER_DISCOVERY;
                fresh.inbound = true;
                node_->peers.push_back (fresh);
                peer = &node_->peers.back ();
            }
            peer->rid = source_rid_;
            peer->state = ZLINK_MESH_PEER_ADMITTED;
            peer->last_error = 0;
            //  Record the endpoint the peer advertises so a manual intent for
            //  the same endpoint merges into one MIXED-source entry.
            if (peer->endpoint.empty () && !descriptor_.advertised_endpoint.empty ())
                peer->endpoint = descriptor_.advertised_endpoint;
            apply_descriptor_locked (peer, descriptor_);
            recompute_readiness_locked (node_);
            if (is_hello_)
                send_admit = true;
        }
    }

    if (generation_drained)
        emit_peer_event (node_, ZLINK_MESH_MONITOR_PEER_DRAINING, source_rid_, 0);
    if (send_admit) {
        std::vector<unsigned char> frame = make_envelope (wire_admit, 0);
        {
            std::lock_guard<std::mutex> lock (node_->mutex);
            encode_descriptor_locked (node_, frame);
        }
        send_control (node_, target, frame);
        emit_peer_event (node_, ZLINK_MESH_MONITOR_PEER_ADMITTED, source_rid_, 0);
    } else if (send_reject) {
        std::vector<unsigned char> frame = make_envelope (wire_reject, 0);
        put_u32 (frame, static_cast<uint32_t> (reject_errno));
        send_control (node_, target, frame);
        emit_peer_event (node_, ZLINK_MESH_MONITOR_PEER_REJECTED, source_rid_, reject_errno);
    } else if (admit_rejected) {
        emit_peer_event (node_, ZLINK_MESH_MONITOR_PEER_REJECTED, source_rid_, admit_reject_errno);
    } else if (!is_hello_) {
        emit_peer_event (node_, ZLINK_MESH_MONITOR_PEER_ADMITTED, source_rid_, 0);
    }
}

void handle_reject (mesh_node_t *node_, const rid_bytes_t &source_rid_, uint32_t reason_)
{
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        //  The rejecting side answered our HELLO; the matching intent is either
        //  keyed by rid (expected) or the connecting intent awaiting its rid.
        peer_state_t *intent = find_peer_by_rid_locked (node_, source_rid_);
        if (!intent || intent->state != ZLINK_MESH_PEER_CONNECTING) {
            intent = NULL;
            for (size_t i = 0; i < node_->peers.size () && !intent; ++i) {
                if (node_->peers[i].state == ZLINK_MESH_PEER_CONNECTING)
                    intent = &node_->peers[i];
            }
        }
        if (!intent)
            return;
        intent->rid = source_rid_;
        intent->state = ZLINK_MESH_PEER_ERROR;
        intent->last_error = static_cast<int32_t> (reason_);
        intent->last_changed_ms = now_ms ();
        recompute_readiness_locked (node_);
    }
    emit_peer_event (node_, ZLINK_MESH_MONITOR_PEER_REJECTED, source_rid_,
                     static_cast<int32_t> (reason_));
}

void handle_update (mesh_node_t *node_,
                    const rid_bytes_t &source_rid_,
                    const wire_descriptor_t &descriptor_)
{
    std::lock_guard<std::mutex> lock (node_->mutex);
    peer_state_t *peer = find_peer_by_rid_locked (node_, source_rid_);
    if (!peer || peer->state != ZLINK_MESH_PEER_ADMITTED)
        return;
    if (peer->lifecycle_generation != descriptor_.lifecycle_generation)
        return;
    apply_descriptor_locked (peer, descriptor_);
}

void handle_peer_down (mesh_node_t *node_, const rid_bytes_t &rid_)
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        //  Every lifetime sharing this RID rides the same transport: the
        //  admitted successor errors out and a DRAINING predecessor closes
        //  with the pipe.
        for (size_t i = 0; i < node_->peers.size (); ++i) {
            peer_state_t &peer = node_->peers[i];
            if (peer.rid != rid_)
                continue;
            if (peer.state == ZLINK_MESH_PEER_ADMITTED) {
                peer.state = ZLINK_MESH_PEER_ERROR;
                peer.last_error = ENOTCONN;
                peer.last_changed_ms = now_ms ();
                changed = true;
            } else if (peer.state == ZLINK_MESH_PEER_DRAINING) {
                peer.state = ZLINK_MESH_PEER_CLOSED;
                peer.last_changed_ms = now_ms ();
            }
        }
        if (!changed)
            return;
        recompute_readiness_locked (node_);
    }
    emit_peer_event (node_, ZLINK_MESH_MONITOR_PEER_CLOSED, rid_, ENOTCONN);
}
}
}
