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

bool apply_transport_ready_locked (peer_state_t *peer_, uint64_t connection_id_)
{
    const bool handover =
      peer_->state == ZLINK_MESH_PEER_ADMITTED
      && !peer_->transport.empty ()
      && !peer_->transport.matches (connection_id_);

    //  ROUTER emits a READY edge only after its duplicate-RID policy has
    //  selected this physical pipe. Move the logical lifetime back through
    //  CONNECTING before its HELLO arrives, so a delayed disconnect from the
    //  replaced pipe cannot make the replacement look like a duplicate
    //  lifetime. A READY edge for the current pipe only fills a transport id
    //  that was missing because HELLO arrived first.
    if (handover || peer_->state == ZLINK_MESH_PEER_ERROR) {
        peer_->state = ZLINK_MESH_PEER_CONNECTING;
        peer_->last_error = 0;
        peer_->last_changed_ms = now_ms ();
    }
    if (peer_->state != ZLINK_MESH_PEER_ADMITTED
        || peer_->transport.empty ())
        peer_->transport = peer_transport_t (connection_id_);
    return handover;
}

void emit_peer_event (mesh_node_t *node_,
                      zlink_mesh_monitor_event_kind_t kind_,
                      const rid_bytes_t &rid_,
                      int32_t error_,
                      uint64_t lifecycle_generation_,
                      uint64_t descriptor_revision_)
{
    zlink_mesh_monitor_event_t event;
    memset (&event, 0, sizeof (event));
    event.kind = kind_;
    if (!rid_.empty ()) {
        event.peer_rid.size = static_cast<uint8_t> (rid_.size ());
        memcpy (event.peer_rid.data, rid_.data (), rid_.size ());
        if (lifecycle_generation_ == 0 || descriptor_revision_ == 0) {
            std::lock_guard<std::mutex> lock (node_->mutex);
            peer_state_t *const peer =
              find_peer_by_rid_locked (node_, rid_);
            if (peer) {
                if (lifecycle_generation_ == 0)
                    lifecycle_generation_ =
                      peer->lifecycle_generation;
                if (descriptor_revision_ == 0)
                    descriptor_revision_ =
                      peer->descriptor_revision;
            }
        }
    }
    event.peer_lifecycle_generation = lifecycle_generation_;
    event.peer_descriptor_revision = descriptor_revision_;
    event.failure_errno = error_;
    if (kind_ == ZLINK_MESH_MONITOR_PEER_REJECTED)
        event.result_code =
          error_ == EACCES
            ? ZLINK_CONNECT_AUTH_FAILED
            : (error_ == EEXIST || error_ == ESTALE || error_ == EADDRINUSE
                 ? ZLINK_CONNECT_CONFLICT
                 : ZLINK_CONNECT_INTERNAL_ERROR);
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

peer_transport_t select_admission_transport (
  bool is_hello_,
  const peer_transport_t &previous_transport_,
  const peer_transport_t *current_transport_)
{
    if (!current_transport_)
        return peer_transport_t ();
    //  A HELLO can arrive before the ROUTER READY edge for a replacement
    //  pipe. In that ordering the RID map still names the predecessor.
    if (is_hello_ && !previous_transport_.empty ()
        && current_transport_->matches (
          previous_transport_.connection_id))
        return peer_transport_t ();
    return *current_transport_;
}

bool is_duplicate_admitted_lifetime (bool is_hello_,
                                     const peer_state_t *existing_,
                                     uint64_t incoming_generation_)
{
    return is_hello_ && existing_
           && existing_->state == ZLINK_MESH_PEER_ADMITTED
           && existing_->lifecycle_generation == incoming_generation_;
}

bool is_repeated_selected_hello (bool is_hello_,
                                 const peer_state_t *existing_,
                                 uint64_t incoming_generation_,
                                 const peer_transport_t &selected_transport_)
{
    return is_duplicate_admitted_lifetime (
             is_hello_, existing_, incoming_generation_)
           && existing_->transport.matches (
             selected_transport_.connection_id);
}

//  Validates an inbound descriptor against local admission rules. Returns 0
//  or -1 with errno describing the conflict class.
int validate_admission_locked (mesh_node_t *node_,
                               const rid_bytes_t &source_rid_,
                               const wire_descriptor_t &descriptor_,
                               const peer_state_t *intent_,
                               bool is_hello_)
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
    if (is_duplicate_admitted_lifetime (
          is_hello_, existing, descriptor_.lifecycle_generation)) {
        //  A second physical connection cannot claim the same logical
        //  lifetime. Same-direction reconnect is admitted only after the old
        //  transport has left ADMITTED; a higher generation replaces it.
        errno = EADDRINUSE;
        return -1;
    }
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
                            bool is_hello_,
                            uint64_t connection_id_)
{
    bool send_admit = false;
    bool send_reject = false;
    int reject_errno = 0;
    bool admit_rejected = false;
    int admit_reject_errno = 0;
    bool generation_drained = false;
    bool repeated_selected_hello = false;
    bool replay_bound_sessions = false;
    uint64_t drained_lifecycle_generation = 0;
    uint64_t drained_descriptor_revision = 0;
    uint64_t admitted_lifecycle_generation = 0;
    uint64_t admitted_descriptor_revision = 0;
    zlink_routing_id_t target = rid_value (source_rid_);
    std::unique_lock<std::mutex> connection_lock (
      node_->peer_connection_mutex);

    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        if (node_->state != ZLINK_MESH_NODE_STARTED && node_->state != ZLINK_MESH_NODE_PARTIAL_READY
            && node_->state != ZLINK_MESH_NODE_READY)
            return;

        peer_state_t *intent = NULL;
        bool merge_inbound_intent = false;
        if (is_hello_ && !descriptor_.advertised_endpoint.empty ()) {
            //  Reciprocal connectors can deliver the remote HELLO before our
            //  outbound ADMIT identifies the configured intent by RID. Match
            //  the peer's advertised endpoint now so both physical directions
            //  share one logical peer row instead of creating a DISCOVERY row
            //  beside the existing MANUAL intent.
            intent = find_intent_by_endpoint_locked (
              node_, descriptor_.advertised_endpoint);
            merge_inbound_intent = intent != NULL;
        } else if (!is_hello_) {
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

        peer_state_t *const selected_peer =
          find_peer_by_rid_locked (node_, source_rid_);
        const std::map<rid_bytes_t, peer_transport_t>::const_iterator
          selected_transport =
            node_->current_peer_transports.find (source_rid_);
        //  A connector pipe is reused across reconnect attempts, so the
        //  connection id stored on that pipe can describe its first
        //  transport. The socket monitor assigns the new attempt's identity
        //  before ROUTER makes its frames readable; keep that selected
        //  transport instead of replacing it with the stale pipe value.
        const peer_transport_t admission_transport =
          selected_transport != node_->current_peer_transports.end ()
            ? selected_transport->second
            : peer_transport_t (connection_id_);
        if (selected_transport == node_->current_peer_transports.end ()
            && connection_id_ != 0)
            node_->current_peer_transports[source_rid_] =
              admission_transport;
        if (!admission_transport.empty ()) {
            if (is_hello_ && selected_peer
                && selected_peer->state == ZLINK_MESH_PEER_ADMITTED
                && selected_peer->lifecycle_generation
                     == descriptor_.lifecycle_generation
                && !selected_peer->transport.matches (
                  admission_transport.connection_id)) {
                selected_peer->state = ZLINK_MESH_PEER_CONNECTING;
                selected_peer->transport = admission_transport;
                selected_peer->last_error = 0;
                selected_peer->last_changed_ms = now_ms ();
            }
        }

        repeated_selected_hello = is_repeated_selected_hello (
          is_hello_, selected_peer, descriptor_.lifecycle_generation,
          admission_transport);
        if (!repeated_selected_hello
            && validate_admission_locked (
                 node_, source_rid_, descriptor_, intent, is_hello_)
            != 0) {
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
            const bool replace_bound_session_route_lifetime =
              !peer || peer->state != ZLINK_MESH_PEER_ADMITTED
              || peer->lifecycle_generation
                   != descriptor_.lifecycle_generation
              || (!admission_transport.empty ()
                  && !peer->transport.matches (
                    admission_transport.connection_id));
            const peer_transport_t previous_transport =
              peer ? peer->transport : peer_transport_t ();
            //  A higher lifecycle generation starts a separate lifetime: the
            //  previous admitted entry stays observable as DRAINING (excluded
            //  from new snapshots, closed with the transport or an explicit
            //  disconnect) and a fresh entry carries the new generation.
            if (peer && peer->state == ZLINK_MESH_PEER_ADMITTED
                && descriptor_.lifecycle_generation > peer->lifecycle_generation) {
                generation_drained = true;
                drained_lifecycle_generation =
                  peer->lifecycle_generation;
                drained_descriptor_revision =
                  peer->descriptor_revision;
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
            if (merge_inbound_intent
                && peer->source == ZLINK_MESH_PEER_MANUAL)
                peer->source = ZLINK_MESH_PEER_MIXED;
            peer->rid = source_rid_;
            const std::map<rid_bytes_t, peer_transport_t>::const_iterator
              transport = node_->current_peer_transports.find (source_rid_);
            peer->transport =
              !admission_transport.empty ()
                ? admission_transport
                : select_admission_transport (
                    is_hello_, previous_transport,
                    transport == node_->current_peer_transports.end ()
                      ? NULL
                      : &transport->second);
            peer->state = ZLINK_MESH_PEER_ADMITTED;
            peer->last_error = 0;
            //  Record the endpoint the peer advertises so a manual intent for
            //  the same endpoint merges into one MIXED-source entry.
            if (peer->endpoint.empty () && !descriptor_.advertised_endpoint.empty ())
                peer->endpoint = descriptor_.advertised_endpoint;
            apply_descriptor_locked (peer, descriptor_);
            if (replace_bound_session_route_lifetime) {
                for (std::map<std::string, bound_session_route_t>::iterator
                       route = node_->bound_session_routes.begin ();
                     route != node_->bound_session_routes.end ();) {
                    if (route->second.source_node_rid == source_rid_)
                        route = node_->bound_session_routes.erase (route);
                    else
                        ++route;
                }
            }
            admitted_lifecycle_generation =
              peer->lifecycle_generation;
            admitted_descriptor_revision =
              peer->descriptor_revision;
            recompute_readiness_locked (node_);
            replay_bound_sessions = !repeated_selected_hello;
            if (is_hello_)
                send_admit = true;
        }
    }

    //  Socket sends can wait for internal send serialization and must not
    //  retain the peer transition mutex. The exact connection requirement
    //  prevents a same-RID successor from receiving this response.
    connection_lock.unlock ();
    if (send_admit) {
        std::vector<unsigned char> frame = make_envelope (wire_admit, 0);
        {
            std::lock_guard<std::mutex> lock (node_->mutex);
            encode_descriptor_locked (node_, frame);
        }
        send_control_exact (node_, target, frame, connection_id_);
    } else if (send_reject) {
        std::vector<unsigned char> frame = make_envelope (wire_reject, 0);
        put_u32 (frame, static_cast<uint32_t> (reject_errno));
        send_control_exact (node_, target, frame, connection_id_);
        //  The ROUTER owns physical duplicate-RID retirement. Disconnecting
        //  by logical RID here can terminate the selected successor instead
        //  of the rejected pipe when reciprocal connectors cross. The peer
        //  receives the terminal REJECT while the ROUTER retires the losing
        //  physical transport by its private identity.
    }

    //  Monitor handlers are user callbacks and may call peer APIs. The
    //  logical/physical transition is complete at this point, so never invoke
    //  a handler while holding the peer transition mutex.
    if (replay_bound_sessions)
        stream_sessions_replay_bound_session_bindings (
          node_, source_rid_);
    if (generation_drained)
        emit_peer_event (
          node_, ZLINK_MESH_MONITOR_PEER_DRAINING, source_rid_, 0,
          drained_lifecycle_generation, drained_descriptor_revision);
    if (send_admit && !repeated_selected_hello) {
        emit_peer_event (
          node_, ZLINK_MESH_MONITOR_PEER_ADMITTED, source_rid_, 0,
          admitted_lifecycle_generation, admitted_descriptor_revision);
    } else if (send_reject) {
        emit_peer_event (
          node_, ZLINK_MESH_MONITOR_PEER_REJECTED, source_rid_,
          reject_errno, descriptor_.lifecycle_generation,
          descriptor_.descriptor_revision);
    } else if (admit_rejected) {
        emit_peer_event (
          node_, ZLINK_MESH_MONITOR_PEER_REJECTED, source_rid_,
          admit_reject_errno, descriptor_.lifecycle_generation,
          descriptor_.descriptor_revision);
    } else if (!is_hello_) {
        emit_peer_event (
          node_, ZLINK_MESH_MONITOR_PEER_ADMITTED, source_rid_, 0,
          admitted_lifecycle_generation, admitted_descriptor_revision);
    }
    if ((send_admit && !send_reject) || (!is_hello_ && !admit_rejected))
        retry_forced_reply_routes (node_);
}

void handle_reject (mesh_node_t *node_, const rid_bytes_t &source_rid_, uint32_t reason_)
{
    uint64_t lifecycle_generation = 0;
    uint64_t descriptor_revision = 0;
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
        lifecycle_generation = intent->lifecycle_generation;
        descriptor_revision = intent->descriptor_revision;
        recompute_readiness_locked (node_);
    }
    emit_peer_event (node_, ZLINK_MESH_MONITOR_PEER_REJECTED, source_rid_,
                     static_cast<int32_t> (reason_),
                     lifecycle_generation, descriptor_revision);
}

void handle_update (mesh_node_t *node_,
                    const rid_bytes_t &source_rid_,
                    const wire_descriptor_t &descriptor_)
{
    std::vector<std::string> changed_channels;
    uint64_t lifecycle_generation = 0;
    uint64_t descriptor_revision = 0;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        peer_state_t *peer = find_peer_by_rid_locked (node_, source_rid_);
        if (!peer || peer->state != ZLINK_MESH_PEER_ADMITTED)
            return;
        if (peer->lifecycle_generation != descriptor_.lifecycle_generation
            || peer->descriptor_revision >= descriptor_.descriptor_revision)
            return;

        const std::map<std::string, uint32_t> previous_channels = peer->channels;
        apply_descriptor_locked (peer, descriptor_);
        lifecycle_generation = peer->lifecycle_generation;
        descriptor_revision = peer->descriptor_revision;

        for (std::map<std::string, uint32_t>::const_iterator it =
               previous_channels.begin ();
             it != previous_channels.end (); ++it) {
            const std::map<std::string, uint32_t>::const_iterator current =
              peer->channels.find (it->first);
            if (current == peer->channels.end () || current->second != it->second)
                changed_channels.push_back (it->first);
        }
        for (std::map<std::string, uint32_t>::const_iterator it =
               peer->channels.begin ();
             it != peer->channels.end (); ++it) {
            if (previous_channels.count (it->first) == 0)
                changed_channels.push_back (it->first);
        }
    }

    //  A remote descriptor update is the authoritative channel-admission
    //  transition. Emit it after releasing the peer lock so monitor handlers
    //  can safely query the updated peer table from their callback.
    for (size_t i = 0; i < changed_channels.size (); ++i) {
        zlink_mesh_monitor_event_t event;
        memset (&event, 0, sizeof (event));
        event.kind = ZLINK_MESH_MONITOR_CHANNEL_CHANGED;
        event.peer_rid.size = static_cast<uint8_t> (source_rid_.size ());
        memcpy (event.peer_rid.data, source_rid_.data (), source_rid_.size ());
        event.peer_lifecycle_generation = lifecycle_generation;
        event.peer_descriptor_revision = descriptor_revision;
        snprintf (event.channel_name, sizeof (event.channel_name), "%s",
                  changed_channels[i].c_str ());
        emit_monitor_event (node_, event);
    }
}

void handle_peer_down (mesh_node_t *node_,
                       const rid_bytes_t &rid_,
    uint64_t connection_id_)
{
    bool changed = false;
    uint64_t lifecycle_generation = 0;
    uint64_t descriptor_revision = 0;
    bool fail_stream_routes = false;
    std::unique_lock<std::mutex> connection_lock (
      node_->peer_connection_mutex);
    if (node_->remote_route_flights > 0) {
        try {
#ifdef ZLINK_BUILD_TESTS
            test_maybe_throw_alloc ();
#endif
            remote_route_key_t key;
            key.rid.size = static_cast<uint8_t> (rid_.size ());
            if (!rid_.empty ())
                memcpy (key.rid.data, rid_.data (), rid_.size ());
            key.connection_id = connection_id_;
            node_->remote_route_disconnects.insert (key);
        }
        catch (const std::bad_alloc &) {
            //  Conservatively reject every flight that commits before the
            //  current flight set drains; this preserves fencing under OOM.
            node_->remote_route_disconnect_overflow = true;
        }
    }
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        const std::map<rid_bytes_t, peer_transport_t>::iterator current =
          node_->current_peer_transports.find (rid_);
        if (current != node_->current_peer_transports.end ()
            && current->second.matches (connection_id_))
            node_->current_peer_transports.erase (current);

        //  A RID can be reused by a higher lifecycle generation while the
        //  predecessor's disconnect is still queued. Attribute the event to
        //  the process-local physical transport identity captured at admission.
        for (size_t i = 0; i < node_->peers.size (); ++i) {
            peer_state_t &peer = node_->peers[i];
            if (peer.rid != rid_
                || !peer.transport.matches (connection_id_))
                continue;
            lifecycle_generation = peer.lifecycle_generation;
            descriptor_revision = peer.descriptor_revision;
            if (peer.state == ZLINK_MESH_PEER_ADMITTED) {
                peer.state = ZLINK_MESH_PEER_ERROR;
                peer.last_error = ENOTCONN;
                peer.last_changed_ms = now_ms ();
                changed = true;
            } else if (peer.state == ZLINK_MESH_PEER_DRAINING) {
                peer.state = ZLINK_MESH_PEER_CLOSED;
                peer.last_changed_ms = now_ms ();
                changed = true;
            }
        }
        bool rid_still_admitted = false;
        for (size_t i = 0; i < node_->peers.size (); ++i) {
            if (node_->peers[i].rid == rid_
                && node_->peers[i].state == ZLINK_MESH_PEER_ADMITTED) {
                rid_still_admitted = true;
                break;
            }
        }
        if (!rid_still_admitted) {
            for (std::map<std::string, bound_session_route_t>::iterator route =
                   node_->bound_session_routes.begin ();
                 route != node_->bound_session_routes.end ();) {
                if (route->second.source_node_rid == rid_)
                    route = node_->bound_session_routes.erase (route);
                else
                    ++route;
            }
            fail_stream_routes = true;
        }
        if (changed)
            recompute_readiness_locked (node_);
    }
    connection_lock.unlock ();
    //  Peer-down is a cold path. Drain matching operations one at a time so
    //  terminalization never allocates a collection after transport loss.
    while (true) {
        zlink_mesh_operation_id_t operation_id;
        memset (&operation_id, 0, sizeof (operation_id));
        bool found = false;
        {
            std::lock_guard<std::mutex> lock (node_->mutex);
            for (std::unordered_map<uint64_t, pending_operation_t>::iterator
                   operation = node_->operations.begin ();
                 operation != node_->operations.end (); ++operation) {
                pending_operation_t &pending = operation->second;
                if (fail_stream_routes && pending.stream_bind_validation
                    && pending.stream_bind_owner_rid == rid_) {
                    operation_id = pending.id;
                    pending.stream_bind_validation = false;
                    found = true;
                    break;
                }
                if (pending.instance_remote_route
                    && pending.instance_remote_route_committed
                    && pending.instance_target_node_rid == rid_
                    && pending.instance_target_connection_id
                         == connection_id_) {
                    operation_id = pending.id;
                    pending.instance_remote_route_committed = false;
                    found = true;
                    break;
                }
            }
        }
        if (!found)
            break;
        (void) complete_pending_operation_by_id (
          node_, operation_id, ZLINK_REQUEST_NOT_CONNECTED, ENOTCONN);
    }
    while (true) {
        uint64_t relay_serial = 0;
        {
            std::lock_guard<std::mutex> lock (node_->mutex);
            for (std::unordered_map<uint64_t, reply_route_t>::iterator route =
                   node_->reply_routes.begin ();
                 route != node_->reply_routes.end (); ++route) {
                reply_route_t &reply = route->second;
                if (reply.instance_downstream_route && !reply.consumed
                    && reply.instance_downstream_rid == rid_
                    && reply.instance_downstream_connection_id
                         == connection_id_) {
                    relay_serial = route->first;
                    reply.instance_downstream_route = false;
                    break;
                }
            }
        }
        if (relay_serial == 0)
            break;
        (void) force_reply_via_route (
          node_, relay_serial, ZLINK_REQUEST_NOT_CONNECTED, ENOTCONN);
    }
    if (changed)
        emit_peer_event (
          node_, ZLINK_MESH_MONITOR_PEER_CLOSED, rid_, ENOTCONN,
          lifecycle_generation, descriptor_revision);
}
}
}

#ifdef ZLINK_BUILD_TESTS
extern "C" void
zlink_test_mesh_inject_disconnect (void *mesh_node_,
                                   const zlink_routing_id_t *peer_rid_,
                                   uint64_t connection_id_)
{
    if (!mesh_node_ || !peer_rid_ || connection_id_ == 0)
        return;
    zlink::mesh::handle_peer_down (
      static_cast<zlink::mesh::mesh_node_t *> (mesh_node_),
      zlink::mesh::rid_bytes (*peer_rid_), connection_id_);
}

extern "C" uint64_t zlink_test_mesh_select_admission_transport (
  int is_hello_, uint64_t previous_connection_id_,
  uint64_t current_connection_id_)
{
    const zlink::mesh::peer_transport_t previous (
      previous_connection_id_);
    const zlink::mesh::peer_transport_t current (
      current_connection_id_);
    return zlink::mesh::select_admission_transport (
             is_hello_ != 0, previous,
             current_connection_id_ == 0 ? NULL : &current)
      .connection_id;
}

extern "C" int zlink_test_mesh_duplicate_admitted_lifetime (
  int is_hello_, uint64_t existing_generation_,
  uint64_t incoming_generation_)
{
    zlink::mesh::peer_state_t existing;
    existing.state = ZLINK_MESH_PEER_ADMITTED;
    existing.lifecycle_generation = existing_generation_;
    return zlink::mesh::is_duplicate_admitted_lifetime (
             is_hello_ != 0, &existing, incoming_generation_)
             ? 1
             : 0;
}

extern "C" int zlink_test_mesh_repeated_selected_hello (
  uint64_t existing_generation_, uint64_t incoming_generation_,
  uint64_t existing_connection_id_, uint64_t selected_connection_id_)
{
    zlink::mesh::peer_state_t existing;
    existing.state = ZLINK_MESH_PEER_ADMITTED;
    existing.lifecycle_generation = existing_generation_;
    existing.transport =
      zlink::mesh::peer_transport_t (existing_connection_id_);
    const zlink::mesh::peer_transport_t selected (
      selected_connection_id_);
    return zlink::mesh::is_repeated_selected_hello (
             true, &existing, incoming_generation_, selected)
             ? 1
             : 0;
}

extern "C" int zlink_test_mesh_transport_ready_transition (
  uint64_t previous_connection_id_, uint64_t ready_connection_id_)
{
    zlink::mesh::peer_state_t peer;
    peer.state = ZLINK_MESH_PEER_ADMITTED;
    peer.transport =
      zlink::mesh::peer_transport_t (previous_connection_id_);
    const bool handover =
      zlink::mesh::apply_transport_ready_locked (
        &peer, ready_connection_id_);
    if (handover)
        return peer.state == ZLINK_MESH_PEER_CONNECTING
                   && peer.transport.matches (ready_connection_id_)
                 ? 1
                 : -1;
    return peer.state == ZLINK_MESH_PEER_ADMITTED
               && peer.transport.matches (ready_connection_id_)
             ? 0
             : -1;
}
#endif
