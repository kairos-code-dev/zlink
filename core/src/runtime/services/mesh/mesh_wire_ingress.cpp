/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <string.h>

#include <memory>
#include <string>
#include <vector>

#include "services/mesh/mesh_wire_internal.hpp"
#include "api/monitoring/monitor_api_internal.hpp"
#include "api/mesh/mesh_c_internal.hpp"
#include "api/socket/socket_api_internal.hpp"
#include "core/recv_internal.hpp"
#include "utils/macros.hpp"

//  Service ingress router: demultiplexes received wire messages into the
//  node/spot/actor/transfer services and runs the ingress thread.
namespace zlink
{
namespace mesh
{
//  --- ingress: data -----------------------------------------------------------------

//  Admits one wire data record into the Node application mailbox. Requests
//  install a remote-origin reply route first so the receiver can answer
//  through the regular one-shot token.
void handle_data (mesh_node_t *node_,
                  const rid_bytes_t &source_rid_,
                  unsigned char type_,
                  uint64_t correlation_,
                  const std::string &channel_,
                  std::vector<unsigned char> *metadata_,
                  std::vector<zlink_msg_t> *parts_)
{
    const bool is_request = type_ == wire_node_request || type_ == wire_channel_request;

    uint64_t origin_generation = 0;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        peer_state_t *peer = find_peer_by_rid_locked (node_, source_rid_);
        if (!peer || peer->state != ZLINK_MESH_PEER_ADMITTED)
            return; //  Unadmitted traffic is dropped by contract.
        origin_generation = peer->lifecycle_generation;
    }

    std::unique_ptr<queued_record_t> record (new (std::nothrow) queued_record_t ());
    if (!record.get ())
        return;
    switch (type_) {
        case wire_node_send:
            record->kind = ZLINK_MESH_RECORD_NODE_SEND;
            break;
        case wire_node_request:
            record->kind = ZLINK_MESH_RECORD_NODE_REQUEST;
            record->operation_kind = ZLINK_MESH_OPERATION_NODE_REQUEST;
            break;
        case wire_channel_send:
            record->kind = ZLINK_MESH_RECORD_CHANNEL_SEND;
            break;
        case wire_channel_request:
        default:
            record->kind = ZLINK_MESH_RECORD_CHANNEL_REQUEST;
            record->operation_kind = ZLINK_MESH_OPERATION_CHANNEL_REQUEST;
            break;
    }
    record->source_node_rid = source_rid_;
    record->channel_name = channel_;
    if (metadata_ && !metadata_->empty ()) {
        record->has_metadata = true;
        record->application_metadata = std::move (*metadata_);
        record->byte_size += record->application_metadata.size ();
    }
    record->parts = std::move (*parts_);
    parts_->clear ();
    for (size_t i = 0; i < record->parts.size (); ++i)
        record->byte_size += zlink_msg_size (&record->parts[i]);

    uint64_t reply_serial = 0;
    if (is_request) {
        std::lock_guard<std::mutex> lock (node_->mutex);
        reply_serial = node_->next_reply_serial++;
        reply_route_t route;
        route.kind = reply_route_t::kind_generic;
        route.requester = node_owner ();
        route.requester_node_generation = node_->lifecycle_generation;
        memset (&route.operation_id, 0, sizeof (route.operation_id));
        route.operation_kind = record->operation_kind;
        route.consumed = false;
        route.remote_origin = true;
        route.origin_rid = source_rid_;
        route.origin_generation = origin_generation;
        route.origin_correlation = correlation_;
        memset (&route.join_actor, 0, sizeof (route.join_actor));
        route.join_target_spot_generation = 0;
        node_->reply_routes[reply_serial] = route;
        record->has_reply_token = true;
        seal_reply_token (node_, reply_serial, &record->reply_token);
    }

    if (admit_record (node_, node_owner (), domain_application, record, false, 0) != 0) {
        //  Route failure at admission: requests answer with a terminal
        //  failure so the requester completes exactly once.
        const int reason = errno;
        if (is_request) {
            {
                std::lock_guard<std::mutex> lock (node_->mutex);
                node_->reply_routes.erase (reply_serial);
            }
            wire_submit_reply (node_, source_rid_, correlation_,
                               reason == EAGAIN ? ZLINK_REQUEST_BACKPRESSURED
                                                : ZLINK_REQUEST_INTERNAL_ERROR,
                               reason, NULL, 0);
        }
    }
}

void close_frames (std::vector<zlink_msg_t> *frames_);

//  Spot direct ingress: locate the target Spot by rid + generation. Requests
//  answer absence/conflict with a terminal completion for exactly-once.
void handle_spot_data (mesh_node_t *node_,
                       const rid_bytes_t &source_rid_,
                       bool is_request_,
                       uint64_t correlation_,
                       const rid_bytes_t &source_spot_rid_,
                       const rid_bytes_t &target_spot_rid_,
                       uint64_t target_spot_generation_,
                       std::vector<unsigned char> *metadata_,
                       std::vector<zlink_msg_t> *parts_)
{
    uint64_t origin_generation = 0;
    bool admitted_peer = false;
    owner_id_t destination;
    bool target_found = false;
    bool generation_conflict = false;
    bool instance_busy = false;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        peer_state_t *peer =
          find_peer_by_rid_locked (node_, source_rid_);
        if (peer && peer->state == ZLINK_MESH_PEER_ADMITTED) {
            admitted_peer = true;
            origin_generation = peer->lifecycle_generation;
        }
        const std::string key (target_spot_rid_.begin (), target_spot_rid_.end ());
        std::map<std::string, spot_state_t>::iterator it = node_->spots.find (key);
        if (it != node_->spots.end ()) {
            if (it->second.generation == target_spot_generation_) {
                if (it->second.kind == ZLINK_SPOT_KIND_INSTANCE) {
                    std::map<std::string, instance_activation_state_t>::iterator
                      activation = node_->instance_activations.find (key);
                    const uint64_t now_ns =
                      zlink::request_timeout::monotonic_now_ns ();
                    if (activation == node_->instance_activations.end ()
                        || activation->second.spot_generation
                             != it->second.generation
                        || activation->second.state
                             != ZLINK_SPOT_ACTIVATION_READY
                        || activation->second.owner_deadline_ns == 0
                        || now_ns >= activation->second.owner_deadline_ns) {
                        instance_busy = true;
                        if (activation != node_->instance_activations.end ()
                            && activation->second.state
                                 == ZLINK_SPOT_ACTIVATION_READY
                            && activation->second.owner_deadline_ns != 0
                            && now_ns
                                 >= activation->second.owner_deadline_ns) {
                            activation->second.state =
                              ZLINK_SPOT_ACTIVATION_CLOSING;
                            it->second.activation_state =
                              ZLINK_SPOT_ACTIVATION_CLOSING;
                            it->second.draining = true;
                        }
                    } else {
                        target_found = true;
                        destination = spot_owner (
                          target_spot_rid_, it->second.generation);
                    }
                } else if (!it->second.draining) {
                    target_found = true;
                    destination = spot_owner (
                      target_spot_rid_, it->second.generation);
                }
            } else {
                generation_conflict = true;
            }
        }
    }
    if (!admitted_peer) {
        close_frames (parts_);
        return;
    }
    if (!target_found) {
        close_frames (parts_);
        if (is_request_) {
            wire_submit_reply (
              node_, source_rid_, correlation_,
              instance_busy ? ZLINK_REQUEST_BUSY
              : generation_conflict ? ZLINK_REQUEST_CONFLICT
                                    : ZLINK_REQUEST_NOT_FOUND,
              instance_busy ? EBUSY : generation_conflict ? ESTALE : ENOENT,
              NULL, 0);
        }
        return;
    }

    std::unique_ptr<queued_record_t> record (new (std::nothrow) queued_record_t ());
    if (!record.get ()) {
        close_frames (parts_);
        return;
    }
    record->kind = is_request_ ? ZLINK_MESH_RECORD_SPOT_REQUEST : ZLINK_MESH_RECORD_SPOT_SEND;
    record->source_node_rid = source_rid_;
    record->source_spot_rid = source_spot_rid_;
    if (metadata_ && !metadata_->empty ()) {
        record->has_metadata = true;
        record->application_metadata = std::move (*metadata_);
        record->byte_size += record->application_metadata.size ();
    }
    record->parts = std::move (*parts_);
    parts_->clear ();
    for (size_t i = 0; i < record->parts.size (); ++i)
        record->byte_size += zlink_msg_size (&record->parts[i]);

    uint64_t reply_serial = 0;
    if (is_request_) {
        std::lock_guard<std::mutex> lock (node_->mutex);
        reply_serial = node_->next_reply_serial++;
        reply_route_t route;
        route.requester = node_owner ();
        route.requester_node_generation = node_->lifecycle_generation;
        route.operation_kind = ZLINK_MESH_OPERATION_SPOT_REQUEST;
        route.remote_origin = true;
        route.origin_rid = source_rid_;
        route.origin_generation = origin_generation;
        route.origin_correlation = correlation_;
        node_->reply_routes[reply_serial] = route;
        record->operation_kind = ZLINK_MESH_OPERATION_SPOT_REQUEST;
        record->has_reply_token = true;
        seal_reply_token (node_, reply_serial, &record->reply_token);
    }

    if (admit_record (node_, destination, domain_application, record, false, 0) != 0) {
        const int reason = errno;
        if (is_request_) {
            {
                std::lock_guard<std::mutex> lock (node_->mutex);
                node_->reply_routes.erase (reply_serial);
            }
            wire_submit_reply (node_, source_rid_, correlation_,
                               reason == EAGAIN ? ZLINK_REQUEST_BACKPRESSURED
                                                : ZLINK_REQUEST_INTERNAL_ERROR,
                               reason, NULL, 0);
        }
    }
}

namespace
{
int32_t instance_terminal_result (int reason_)
{
    switch (reason_) {
        case EAGAIN:
            return ZLINK_REQUEST_BACKPRESSURED;
        case EEXIST:
        case ESTALE:
            return ZLINK_REQUEST_CONFLICT;
        case EBUSY:
        case ESHUTDOWN:
            return ZLINK_REQUEST_BUSY;
        case ENOENT:
            return ZLINK_REQUEST_NOT_FOUND;
        case ETIMEDOUT:
            return ZLINK_REQUEST_TIMED_OUT;
        case EACCES:
            return ZLINK_REQUEST_REJECTED;
        case EPROTO:
        case EINVAL:
            return ZLINK_REQUEST_PROTOCOL_ERROR;
        case ENOTCONN:
            return ZLINK_REQUEST_NOT_CONNECTED;
        default:
            return ZLINK_REQUEST_INTERNAL_ERROR;
    }
}
}

//  Instance ingress first establishes the ordinary one-shot reply route, then
//  hands the complete record to the Instance activation owner. Cold placement
//  is exposed as Node infrastructure work; redirects use exact direct admission.
void handle_instance_data (
  mesh_node_t *node_,
  const rid_bytes_t &transport_source_rid_,
  uint64_t sender_generation_,
  const rid_bytes_t &logical_source_rid_,
  const rid_bytes_t &source_spot_rid_,
  const instance_placement_value_t &target_,
  zlink_instance_spot_operation_kind_t operation_kind_,
  const zlink_mesh_operation_id_t &operation_id_,
  uint32_t timeout_ms_,
  bool redirected_,
  uint64_t redirected_spot_generation_,
  uint64_t relay_serial_,
  std::vector<unsigned char> *metadata_,
  std::vector<zlink_msg_t> *parts_)
{
    const bool is_request =
      operation_kind_ == ZLINK_INSTANCE_SPOT_OPERATION_REQUEST;
    uint64_t origin_generation = 0;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        peer_state_t *peer =
          find_peer_by_rid_locked (node_, transport_source_rid_);
        if (!peer || peer->state != ZLINK_MESH_PEER_ADMITTED
            || peer->lifecycle_generation != sender_generation_) {
            close_frames (parts_);
            return;
        }
        origin_generation = peer->lifecycle_generation;
    }

    std::unique_ptr<queued_record_t> record (
      new (std::nothrow) queued_record_t ());
    if (!record.get ()) {
        close_frames (parts_);
        return;
    }
    record->kind = is_request ? ZLINK_MESH_RECORD_SPOT_REQUEST
                              : ZLINK_MESH_RECORD_SPOT_SEND;
    record->source_node_rid = logical_source_rid_;
    record->source_spot_rid = source_spot_rid_;
    record->operation_id = operation_id_;
    if (is_request && timeout_ms_ != 0)
        record->deadline_ns =
          zlink::request_timeout::deadline_after_ms (timeout_ms_);
    if (is_request)
        record->operation_kind = ZLINK_MESH_OPERATION_SPOT_REQUEST;
    if (metadata_ && !metadata_->empty ()) {
        record->has_metadata = true;
        record->application_metadata = std::move (*metadata_);
        record->byte_size += record->application_metadata.size ();
    }
    record->parts = std::move (*parts_);
    parts_->clear ();
    for (size_t i = 0; i < record->parts.size (); ++i)
        record->byte_size += zlink_msg_size (&record->parts[i]);

    uint64_t reply_serial = 0;
    if (is_request) {
        std::lock_guard<std::mutex> lock (node_->mutex);
        reply_serial = node_->next_reply_serial++;
        reply_route_t route;
        route.kind = redirected_ ? reply_route_t::kind_transfer_relay
                                 : reply_route_t::kind_generic;
        route.requester = node_owner ();
        route.requester_node_generation = node_->lifecycle_generation;
        route.operation_id = operation_id_;
        route.operation_kind = ZLINK_MESH_OPERATION_SPOT_REQUEST;
        route.instance_request = true;
        route.remote_origin = true;
        route.origin_rid = transport_source_rid_;
        route.origin_generation = origin_generation;
        route.origin_correlation = redirected_ ? relay_serial_
                                               : operation_id_.low;
        node_->reply_routes[reply_serial] = route;
        record->has_reply_token = true;
        seal_reply_token (node_, reply_serial, &record->reply_token);
    }

    const int admission_rc = redirected_
                               ? admit_instance_direct_record (
                                   node_, target_.spot_rid,
                                   redirected_spot_generation_, record)
                               : admit_instance_record (
                                   node_, target_, record);
    if (admission_rc == 0) {
        if (is_request && timeout_ms_ != 0)
            (void) arm_instance_record_deadline (
              node_, logical_source_rid_, operation_id_);
        return;
    }

    const int reason = errno;
    if (is_request) {
        {
            std::lock_guard<std::mutex> lock (node_->mutex);
            node_->reply_routes.erase (reply_serial);
        }
        if (redirected_)
            (void) wire_submit_reply_relay (
              node_, transport_source_rid_, relay_serial_,
              instance_terminal_result (reason), reason, NULL, 0);
        else
            (void) wire_submit_reply (
              node_, transport_source_rid_, operation_id_.low,
              instance_terminal_result (reason), reason, NULL, 0);
    }
}

//  Multicast ingress: fan out to local subscription matches when this node
//  is a member of the channel. Local budget misses drop that Spot only.
void handle_multicast (mesh_node_t *node_,
                       const rid_bytes_t &source_rid_,
                       const std::string &channel_,
                       const std::string &topic_,
                       const rid_bytes_t &source_spot_rid_,
                       std::vector<unsigned char> *metadata_,
                       std::vector<zlink_msg_t> *parts_)
{
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        peer_state_t *peer =
          find_peer_by_rid_locked (node_, source_rid_);
        if (!peer || peer->state != ZLINK_MESH_PEER_ADMITTED) {
            close_frames (parts_);
            return;
        }
    }

    size_t payload_bytes = 0;
    for (size_t i = 0; i < parts_->size (); ++i)
        payload_bytes += zlink_msg_size (&(*parts_)[i]);

    uint32_t dropped = 0;
    std::vector<owner_id_t> local_targets;
    try {
        std::lock_guard<std::mutex> lock (node_->mutex);
        if (node_->channels.count (channel_) == 0) {
            //  Not a member: the sender snapshot was stale; drop silently.
        } else {
            for (std::map<std::string, spot_state_t>::iterator it = node_->spots.begin ();
                 it != node_->spots.end (); ++it) {
                spot_state_t &spot = it->second;
                if (spot.draining)
                    continue;
                bool match = false;
                for (std::set<subscription_key_t>::const_iterator sub =
                       spot.subscriptions.begin ();
                     sub != spot.subscriptions.end () && !match; ++sub) {
                    if (sub->channel != channel_)
                        continue;
                    if (sub->kind == ZLINK_SPOT_SUBSCRIPTION_EXACT)
                        match = sub->filter == topic_;
                    else
                        match = topic_.compare (0, sub->filter.size (), sub->filter) == 0;
                }
                if (!match)
                    continue;
                local_targets.push_back (spot_owner (spot.rid, spot.generation));
            }
        }
    }
    catch (const std::bad_alloc &) {
        close_frames (parts_);
        return;
    }

    //  Use the same mailbox admission owner as origin-side fan-out. It
    //  applies the lifecycle/fence/budget policy but suppresses target-level
    //  BACKPRESSURED events because multicast reports one aggregate event.
    for (size_t target = 0; target < local_targets.size (); ++target) {
        std::unique_ptr<queued_record_t> record (new (std::nothrow) queued_record_t ());
        if (!record.get ()) {
            ++dropped;
            continue;
        }
        bool copy_failed = false;
        try {
            record->kind = ZLINK_MESH_RECORD_SPOT_MULTICAST;
            record->source_node_rid = source_rid_;
            record->source_spot_rid = source_spot_rid_;
            record->channel_name = channel_;
            record->topic = topic_;
            if (metadata_ && !metadata_->empty ()) {
                record->has_metadata = true;
                record->application_metadata = *metadata_;
                record->byte_size += record->application_metadata.size ();
            }
            record->parts.reserve (parts_->size ());
            for (size_t i = 0; i < parts_->size (); ++i) {
                zlink_msg_t copy;
                if (zlink_msg_init (&copy) != 0) {
                    copy_failed = true;
                    break;
                }
                if (zlink_msg_copy (&copy, &(*parts_)[i]) != 0) {
                    zlink_msg_close (&copy);
                    copy_failed = true;
                    break;
                }
                record->parts.push_back (copy);
            }
            record->byte_size += payload_bytes;
        }
        catch (const std::bad_alloc &) {
            copy_failed = true;
        }
        if (copy_failed
            || admit_multicast_record (node_, local_targets[target], record) != 0) {
            ++dropped;
        }
    }
    close_frames (parts_);
    if (dropped > 0) {
        zlink_mesh_monitor_event_t event;
        memset (&event, 0, sizeof (event));
        event.kind = ZLINK_MESH_MONITOR_MULTICAST_DROPPED;
        event.snapshot_local_spot_count =
          static_cast<uint32_t> (local_targets.size ());
        event.admitted_local_spot_count =
          event.snapshot_local_spot_count - dropped;
        event.dropped_local_spot_count = dropped;
        snprintf (event.channel_name, sizeof (event.channel_name), "%s", channel_.c_str ());
        emit_monitor_event (node_, event);
    }
}

//  Actor data ingress: the destination validates the generation and admits
//  into the actor's application mailbox; requests install a remote-origin
//  generic reply route.
void handle_actor_data (mesh_node_t *node_,
                        const rid_bytes_t &source_rid_,
                        const rid_bytes_t &source_spot_rid_,
                        uint64_t source_binding_generation_,
                        bool is_request_,
                        uint64_t correlation_,
                        const zlink_actor_ref_t &source_actor_,
                        bool has_source_actor_,
                        const zlink_actor_ref_t &target_actor_,
                        std::vector<unsigned char> *metadata_,
                        std::vector<zlink_msg_t> *parts_)
{
    uint64_t origin_generation = 0;
    owner_id_t destination;
    rid_bytes_t forward_target;
    bool forward = false;
    int fail_errno = 0;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        peer_state_t *peer = find_peer_by_rid_locked (node_, source_rid_);
        if (!peer || peer->state != ZLINK_MESH_PEER_ADMITTED) {
            close_frames (parts_);
            return;
        }
        origin_generation = peer->lifecycle_generation;
        const std::string id (target_actor_.actor_id);
        std::map<std::string, actor_state_t>::iterator it = node_->actors.find (id);
        if (it == node_->actors.end ()) {
            std::map<std::string, actor_forward_route_t>::iterator route =
              node_->actor_forward_routes.find (id);
            if (!is_request_ && route != node_->actor_forward_routes.end ()
                && route->second.actor_generation == target_actor_.generation) {
                forward_target = route->second.target_node_rid;
                forward = true;
            } else {
                fail_errno = ENOENT;
            }
        }
        else if (it->second.generation != target_actor_.generation || it->second.draining)
            fail_errno = ESTALE;
        else
            destination = actor_owner (id, it->second.generation);
    }
    if (forward) {
        zlink_mesh_metadata_view_t metadata;
        const zlink_mesh_metadata_view_t *metadata_ptr = NULL;
        if (metadata_ && !metadata_->empty ()) {
            metadata.data = metadata_->data ();
            metadata.size = metadata_->size ();
            metadata_ptr = &metadata;
        }
        if (!source_spot_rid_.empty ()) {
            (void) wire_submit_bound_actor_data (
              node_, forward_target, false, 0, source_spot_rid_,
              source_binding_generation_, target_actor_,
              metadata_ptr, parts_->empty () ? NULL : &(*parts_)[0],
              parts_->size (), ZLINK_SEND_FLAGS_DONTWAIT);
        } else {
            (void) wire_submit_actor_data (
              node_, forward_target, false, 0,
              has_source_actor_ ? &source_actor_ : NULL, target_actor_, metadata_ptr,
              parts_->empty () ? NULL : &(*parts_)[0], parts_->size (),
              ZLINK_SEND_FLAGS_DONTWAIT);
        }
        close_frames (parts_);
        return;
    }
    if (fail_errno != 0) {
        close_frames (parts_);
        if (is_request_) {
            wire_submit_reply (node_, source_rid_, correlation_,
                               fail_errno == ENOENT ? ZLINK_REQUEST_NOT_FOUND
                                                    : ZLINK_REQUEST_CONFLICT,
                               fail_errno, NULL, 0);
        }
        return;
    }

    std::unique_ptr<queued_record_t> record (new (std::nothrow) queued_record_t ());
    if (!record.get ()) {
        close_frames (parts_);
        return;
    }
    record->kind = is_request_ ? ZLINK_MESH_RECORD_ACTOR_REQUEST : ZLINK_MESH_RECORD_ACTOR_SEND;
    record->source_node_rid = source_rid_;
    record->source_spot_rid = source_spot_rid_;
    record->source_binding_generation = source_binding_generation_;
    if (has_source_actor_)
        record->source_actor = source_actor_;
    if (metadata_ && !metadata_->empty ()) {
        record->has_metadata = true;
        record->application_metadata = std::move (*metadata_);
        record->byte_size += record->application_metadata.size ();
    }
    record->parts = std::move (*parts_);
    parts_->clear ();
    for (size_t i = 0; i < record->parts.size (); ++i)
        record->byte_size += zlink_msg_size (&record->parts[i]);

    uint64_t reply_serial = 0;
    if (is_request_) {
        std::lock_guard<std::mutex> lock (node_->mutex);
        reply_serial = node_->next_reply_serial++;
        reply_route_t route;
        route.requester = node_owner ();
        route.requester_node_generation = node_->lifecycle_generation;
        route.operation_kind = ZLINK_MESH_OPERATION_ACTOR_REQUEST;
        route.remote_origin = true;
        route.origin_rid = source_rid_;
        route.origin_generation = origin_generation;
        route.origin_correlation = correlation_;
        node_->reply_routes[reply_serial] = route;
        record->operation_kind = ZLINK_MESH_OPERATION_ACTOR_REQUEST;
        record->has_reply_token = true;
        seal_reply_token (node_, reply_serial, &record->reply_token);
    }

    if (admit_record (node_, destination, domain_application, record, false, 0) != 0) {
        const int reason = errno;
        if (is_request_) {
            {
                std::lock_guard<std::mutex> lock (node_->mutex);
                node_->reply_routes.erase (reply_serial);
            }
            wire_submit_reply (node_, source_rid_, correlation_,
                               reason == EAGAIN ? ZLINK_REQUEST_BACKPRESSURED
                                                : ZLINK_REQUEST_INTERNAL_ERROR,
                               reason, NULL, 0);
        }
    }
}

void handle_actor_lookup (mesh_node_t *node_,
                          const rid_bytes_t &source_rid_,
                          uint64_t correlation_,
                          const std::string &actor_id_)
{
    zlink_actor_ref_t ref;
    rid_bytes_t spot_rid;
    uint64_t spot_generation = 0;
    uint64_t membership_epoch = 0;
    if (actor_lookup_local (node_, actor_id_, &ref, &spot_rid, &spot_generation,
                            &membership_epoch)
        == 0) {
        wire_submit_lookup_reply (node_, source_rid_, correlation_, ref, spot_rid,
                                  spot_generation, membership_epoch);
    } else {
        wire_submit_reply (node_, source_rid_, correlation_, ZLINK_REQUEST_NOT_FOUND, ENOENT,
                           NULL, 0);
    }
}

void handle_actor_destroy (mesh_node_t *node_,
                           const rid_bytes_t &source_rid_,
                           uint64_t correlation_,
                           const zlink_actor_ref_t &actor_)
{
    if (actor_destroy_local (node_, &actor_) == 0) {
        wire_submit_reply (node_, source_rid_, correlation_, ZLINK_REQUEST_OK, 0, NULL, 0);
    } else {
        const int reason = errno;
        wire_submit_reply (node_, source_rid_, correlation_,
                           reason == ENOENT ? ZLINK_REQUEST_NOT_FOUND : ZLINK_REQUEST_CONFLICT,
                           reason, NULL, 0);
    }
}

void handle_actor_join (mesh_node_t *node_,
                        const rid_bytes_t &source_rid_,
                        uint64_t correlation_,
                        const zlink_actor_ref_t &actor_,
                        bool entry_,
                        const rid_bytes_t &spot_rid_,
                        uint64_t spot_generation_,
                        std::vector<zlink_msg_t> *parts_)
{
    uint64_t origin_generation = 0;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        peer_state_t *peer = find_peer_by_rid_locked (node_, source_rid_);
        if (!peer || peer->state != ZLINK_MESH_PEER_ADMITTED) {
            close_frames (parts_);
            return;
        }
        origin_generation = peer->lifecycle_generation;
    }
    if (actor_admit_remote_join (node_, source_rid_, origin_generation, correlation_, actor_,
                                 entry_, spot_rid_, spot_generation_, parts_)
        != 0) {
        const int reason = errno;
        close_frames (parts_);
        wire_submit_reply (node_, source_rid_, correlation_,
                           reason == ESTALE  ? ZLINK_REQUEST_CONFLICT
                           : reason == EAGAIN ? ZLINK_REQUEST_BACKPRESSURED
                                              : ZLINK_REQUEST_INTERNAL_ERROR,
                           reason, NULL, 0);
    }
}

//  The previous Spot's node observes the departure as a LEFT control record.
void handle_actor_left (mesh_node_t *node_,
                        const rid_bytes_t &source_rid_,
                        const zlink_actor_ref_t &actor_,
                        const rid_bytes_t &previous_spot_rid_,
                        uint64_t previous_spot_generation_,
                        uint64_t previous_membership_epoch_,
                        uint64_t current_membership_epoch_)
{
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        peer_state_t *peer =
          find_peer_by_rid_locked (node_, source_rid_);
        if (!peer || peer->state != ZLINK_MESH_PEER_ADMITTED)
            return;
    }
    owner_id_t spot_owner_id;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        const std::string key (previous_spot_rid_.begin (), previous_spot_rid_.end ());
        std::map<std::string, spot_state_t>::iterator it = node_->spots.find (key);
        if (it == node_->spots.end () || it->second.generation != previous_spot_generation_)
            return;
        if (it->second.active_actor_count > 0)
            it->second.active_actor_count -= 1;
        spot_owner_id = spot_owner (previous_spot_rid_, previous_spot_generation_);
    }

    std::unique_ptr<queued_record_t> record (new (std::nothrow) queued_record_t ());
    if (!record.get ())
        return;
    record->kind = ZLINK_MESH_RECORD_SPOT_CONTROL;
    record->source_node_rid = source_rid_;
    zlink_actor_control_record_t data;
    memset (&data, 0, sizeof (data));
    data.struct_size = sizeof (data);
    data.version = 1;
    data.kind = ZLINK_ACTOR_LIFECYCLE_LEFT;
    data.previous_actor = actor_;
    data.current_actor = actor_;
    data.previous_spot_rid = rid_value (previous_spot_rid_);
    data.previous_spot_generation = previous_spot_generation_;
    data.previous_membership_epoch = previous_membership_epoch_;
    data.current_membership_epoch = current_membership_epoch_;
    data.result_code = ZLINK_REQUEST_OK;
    record->kind_data.assign (reinterpret_cast<unsigned char *> (&data),
                              reinterpret_cast<unsigned char *> (&data) + sizeof (data));
    (void) admit_record (node_, spot_owner_id, domain_infrastructure, record, false, 0);
    //  The remote leave may have been the Spot's last reference: end the
    //  logical Spot once nothing else (facade, timer, claim) holds it. An
    //  unreferenced Spot cannot observe the LEFT record anyway.
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        maybe_end_spot_locked (node_,
                               std::string (previous_spot_rid_.begin (),
                                            previous_spot_rid_.end ()));
    }
}

//  The target admission callback has already accepted the join. The source
//  sends this notification only after its membership commit, so this record
//  is the post-commit JOINED lifecycle event rather than another admission.
void handle_actor_joined (mesh_node_t *node_,
                          const rid_bytes_t &source_rid_,
                          const zlink_actor_ref_t &actor_,
                          const rid_bytes_t &previous_spot_rid_,
                          uint64_t previous_spot_generation_,
                          uint64_t previous_membership_epoch_,
                          const rid_bytes_t &current_spot_rid_,
                          uint64_t current_spot_generation_,
                          uint64_t current_membership_epoch_)
{
    owner_id_t target_owner;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        peer_state_t *peer = find_peer_by_rid_locked (node_, source_rid_);
        if (!peer || peer->state != ZLINK_MESH_PEER_ADMITTED)
            return;
        const std::string key (current_spot_rid_.begin (), current_spot_rid_.end ());
        std::map<std::string, spot_state_t>::iterator spot = node_->spots.find (key);
        if (spot == node_->spots.end ()
            || spot->second.generation != current_spot_generation_)
            return;
        target_owner = spot_owner (current_spot_rid_, current_spot_generation_);
    }

    std::unique_ptr<queued_record_t> record (new (std::nothrow) queued_record_t ());
    if (!record.get ())
        return;
    record->kind = ZLINK_MESH_RECORD_SPOT_CONTROL;
    record->source_node_rid = source_rid_;
    zlink_actor_control_record_t data;
    memset (&data, 0, sizeof (data));
    data.struct_size = sizeof (data);
    data.version = 1;
    data.kind = ZLINK_ACTOR_LIFECYCLE_JOINED;
    data.previous_actor = actor_;
    data.current_actor = actor_;
    data.previous_spot_rid = rid_value (previous_spot_rid_);
    data.previous_spot_generation = previous_spot_generation_;
    data.previous_membership_epoch = previous_membership_epoch_;
    data.current_spot_rid = rid_value (current_spot_rid_);
    data.current_spot_generation = current_spot_generation_;
    data.current_membership_epoch = current_membership_epoch_;
    data.result_code = ZLINK_REQUEST_OK;
    record->kind_data.assign (reinterpret_cast<unsigned char *> (&data),
                              reinterpret_cast<unsigned char *> (&data) + sizeof (data));
    (void) admit_record (
      node_, target_owner, domain_infrastructure, record, false, 0);
}

void handle_reply_tail (mesh_node_t *node_,
                        const pending_operation_t &op,
                        const rid_bytes_t &source_rid_,
                        int32_t terminal_result_,
                        int32_t failure_errno_,
                        wire_reader_t &tail_,
                        std::vector<zlink_msg_t> *parts_);

void handle_reply (mesh_node_t *node_,
                   const rid_bytes_t &source_rid_,
                   uint64_t correlation_,
                   int32_t terminal_result_,
                   int32_t failure_errno_,
                   wire_reader_t &tail_,
                   std::vector<zlink_msg_t> *parts_)
{
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        peer_state_t *peer =
          find_peer_by_rid_locked (node_, source_rid_);
        if (!peer || peer->state != ZLINK_MESH_PEER_ADMITTED)
            return;
    }
    zlink_mesh_operation_id_t operation_id;
    bool needs_operation_state = false;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        std::unordered_map<uint64_t, pending_operation_t>::iterator it =
          node_->operations.find (correlation_);
        if (it == node_->operations.end ())
            return; //  Already completed (timeout/shutdown): exactly-once.
        operation_id = it->second.id;
        needs_operation_state =
          (it->second.kind == ZLINK_MESH_OPERATION_STREAM_BIND
           && it->second.stream_bind_validation)
          || it->second.kind == ZLINK_MESH_OPERATION_ACTOR_JOIN
          || (it->second.kind == ZLINK_MESH_OPERATION_ACTOR_LOOKUP
              && terminal_result_ == ZLINK_REQUEST_OK);
    }

    if (!needs_operation_state) {
        (void) complete_pending_operation_by_id (
          node_, operation_id, terminal_result_, failure_errno_, NULL,
          parts_->empty () ? NULL : parts_);
        return;
    }

    //  Parse the operation-specific tail before the pending operation is
    //  removed. Allocation failure degrades to an INTERNAL_ERROR completion;
    //  the preallocated completion reservation keeps the operation available
    //  until terminal mailbox admission commits.
    pending_operation_t op;
    try {
        {
            std::lock_guard<std::mutex> lock (node_->mutex);
            std::unordered_map<uint64_t, pending_operation_t>::iterator it =
              node_->operations.find (operation_id.low);
            if (it == node_->operations.end ()
                || it->second.id.high != operation_id.high)
                return;
            op = it->second;
        }
        handle_reply_tail (node_, op, source_rid_, terminal_result_, failure_errno_, tail_,
                           parts_);
    }
    catch (const std::bad_alloc &) {
        (void) complete_pending_operation_by_id (
          node_, operation_id, ZLINK_REQUEST_INTERNAL_ERROR, ENOMEM);
    }
}

void handle_reply_tail (mesh_node_t *node_,
                        const pending_operation_t &op,
                        const rid_bytes_t &source_rid_,
                        int32_t terminal_result_,
                        int32_t failure_errno_,
                        wire_reader_t &tail_,
                        std::vector<zlink_msg_t> *parts_)
{
    if (op.kind == ZLINK_MESH_OPERATION_STREAM_BIND
        && op.stream_bind_validation) {
        uint64_t binding_generation = 0;
        uint64_t membership_epoch = 0;
        if (terminal_result_ == ZLINK_REQUEST_OK) {
            binding_generation = tail_.u64 ();
            membership_epoch = tail_.u64 ();
            if (tail_.failed || binding_generation == 0
                || membership_epoch == 0) {
                (void) complete_pending_operation (
                  node_, op, ZLINK_REQUEST_PROTOCOL_ERROR, EPROTO, NULL,
                  NULL);
                return;
            }
        }
        stream_sessions_apply_remote_bind_reply (
          node_, op, source_rid_, terminal_result_, failure_errno_,
          binding_generation, membership_epoch);
        return;
    }
    if (op.kind == ZLINK_MESH_OPERATION_ACTOR_JOIN) {
        uint32_t join_result = ZLINK_ACTOR_JOIN_REJECTED;
        rid_bytes_t spot_rid;
        uint64_t spot_generation = 0;
        if (terminal_result_ == ZLINK_REQUEST_OK || terminal_result_ == ZLINK_REQUEST_REJECTED) {
            join_result = tail_.u32 ();
            spot_rid = read_rid (tail_);
            spot_generation = tail_.u64 ();
            if (tail_.failed) {
                (void) complete_pending_operation (node_, op, ZLINK_REQUEST_PROTOCOL_ERROR,
                                                   EPROTO, NULL, NULL);
                return;
            }
            actor_apply_remote_join_reply (node_, op, join_result, source_rid_, spot_rid,
                                           spot_generation,
                                           parts_->empty () ? NULL : parts_);
            return;
        }
        (void) complete_pending_operation (node_, op, terminal_result_, failure_errno_, NULL,
                                           NULL);
        return;
    }
    if (op.kind == ZLINK_MESH_OPERATION_ACTOR_LOOKUP && terminal_result_ == ZLINK_REQUEST_OK) {
        zlink_actor_location_t location;
        memset (&location, 0, sizeof (location));
        location.struct_size = sizeof (location);
        location.version = 1;
        const size_t id_len = tail_.u8 ();
        const std::string id = tail_.bytes (id_len);
        snprintf (location.actor.actor_id, sizeof (location.actor.actor_id), "%.*s",
                  (int) id.size (), id.data ());
        location.actor.generation = tail_.u64 ();
        location.actor.node_rid = rid_value (source_rid_);
        const rid_bytes_t spot_rid = read_rid (tail_);
        location.spot_rid = rid_value (spot_rid);
        location.spot_generation = tail_.u64 ();
        location.membership_epoch = tail_.u64 ();
        if (tail_.failed) {
            (void) complete_pending_operation (node_, op, ZLINK_REQUEST_PROTOCOL_ERROR, EPROTO,
                                               NULL, NULL);
            return;
        }
        std::vector<unsigned char> kind_data (
          reinterpret_cast<unsigned char *> (&location),
          reinterpret_cast<unsigned char *> (&location) + sizeof (location));
        (void) complete_pending_operation (node_, op, terminal_result_, failure_errno_,
                                           &kind_data, NULL);
        return;
    }

    (void) complete_pending_operation (node_, op, terminal_result_, failure_errno_, NULL,
                                       parts_->empty () ? NULL : parts_);
}

//  --- ingress: message pump -----------------------------------------------------------

//  Receives every frame of one wire message. Returns false when no message
//  is available.
bool recv_wire_message (mesh_node_t *node_,
                        rid_bytes_t *source_out_,
                        uint64_t *connection_id_out_,
                        std::vector<zlink_msg_t> *frames_out_)
{
    zlink_routing_id_t source_rid;
    memset (&source_rid, 0, sizeof (source_rid));
    uint64_t connection_id = 0;
    zlink_msg_t part;
    if (zlink_msg_init (&part) != 0)
        return false;
    if (zlink::recv_msg_routed_internal (
          node_->router_socket, &part, &source_rid, ZLINK_DONTWAIT,
          &connection_id)
        < 0) {
        zlink_msg_close (&part);
        return false;
    }
    *source_out_ =
      source_rid.size > 0 ? rid_bytes (source_rid) : rid_bytes_t ();
    if (connection_id_out_)
        *connection_id_out_ = connection_id;
    bool has_more = zlink::msg_frame_has_more (part);
    frames_out_->push_back (part);
    while (has_more) {
        zlink_msg_t next;
        if (zlink_msg_init (&next) != 0)
            break;
        zlink_routing_id_t next_rid;
        memset (&next_rid, 0, sizeof (next_rid));
        uint64_t next_connection_id = 0;
        if (zlink::recv_msg_routed_internal (
              node_->router_socket, &next, &next_rid, 0,
              &next_connection_id)
            < 0) {
            zlink_msg_close (&next);
            break;
        }
        if (next_connection_id != connection_id
            || next_rid.size != source_rid.size
            || memcmp (next_rid.data, source_rid.data, source_rid.size) != 0) {
            zlink_msg_close (&next);
            break;
        }
        has_more = zlink::msg_frame_has_more (next);
        frames_out_->push_back (next);
    }
    return true;
}

void close_frames (std::vector<zlink_msg_t> *frames_)
{
    for (size_t i = 0; i < frames_->size (); ++i)
        zlink_msg_close (&(*frames_)[i]);
    frames_->clear ();
}

bool is_current_wire_transport (mesh_node_t *node_,
                                const rid_bytes_t &source_rid_,
                                uint64_t connection_id_,
                                bool require_admitted_)
{
    if (connection_id_ == 0)
        return false;
    std::lock_guard<std::mutex> connection_lock (
      node_->peer_connection_mutex);
    std::lock_guard<std::mutex> lock (node_->mutex);
    const std::map<rid_bytes_t, peer_transport_t>::const_iterator current =
      node_->current_peer_transports.find (source_rid_);
    if (current == node_->current_peer_transports.end ()
        || !current->second.matches (connection_id_))
        return false;
    for (size_t i = 0; i < node_->peers.size (); ++i) {
        const peer_state_t &peer = node_->peers[i];
        if (peer.rid == source_rid_
            && peer.transport.matches (connection_id_)
            && (peer.state == ZLINK_MESH_PEER_ADMITTED
                || (!require_admitted_
                    && peer.state == ZLINK_MESH_PEER_CONNECTING)))
            return true;
    }
    return false;
}

bool is_unselected_or_current_wire_transport (mesh_node_t *node_,
                                              const rid_bytes_t &source_rid_,
                                              uint64_t connection_id_)
{
    if (connection_id_ == 0)
        return false;
    std::lock_guard<std::mutex> connection_lock (
      node_->peer_connection_mutex);
    std::lock_guard<std::mutex> lock (node_->mutex);
    const std::map<rid_bytes_t, peer_transport_t>::const_iterator current =
      node_->current_peer_transports.find (source_rid_);
    return current == node_->current_peer_transports.end ()
           || current->second.matches (connection_id_);
}

void dispatch_wire_message (mesh_node_t *node_,
                            const rid_bytes_t &source_rid_,
                            uint64_t connection_id_,
                            std::vector<zlink_msg_t> *frames_)
{
    if (source_rid_.empty () || frames_->empty ()) {
        close_frames (frames_);
        return;
    }

    //  MeshNode MaxMessageSize is a live option, while an established
    //  transport engine owns the decoder options that existed when its pipe
    //  was created. Enforce the current node value at the complete wire
    //  message boundary so both lowering and raising the limit take effect
    //  without reconnecting peers. Dropping before envelope decoding keeps
    //  oversized application payloads out of every owner mailbox; a request
    //  whose header cannot be admitted completes through its normal timeout.
    int64_t max_message_size = -1;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        max_message_size = node_->max_msg_size;
    }
    if (max_message_size >= 0) {
        const size_t limit = static_cast<size_t> (max_message_size);
        size_t total = 0;
        bool oversized = false;
        for (size_t i = 0; i < frames_->size (); ++i) {
            const size_t frame_size = zlink_msg_size (&(*frames_)[i]);
            if (frame_size > limit || total > limit - frame_size) {
                oversized = true;
                break;
            }
            total += frame_size;
        }
        if (oversized) {
            close_frames (frames_);
            return;
        }
    }

    zlink_msg_t &head = (*frames_)[0];
    wire_reader_t reader (static_cast<const unsigned char *> (zlink_msg_data (&head)),
                          zlink_msg_size (&head));
    if (reader.u8 () != wire_magic_0 || reader.u8 () != wire_magic_1
        || reader.u8 () != wire_version) {
        close_frames (frames_);
        emit_peer_event (node_, ZLINK_MESH_MONITOR_PROTOCOL_ERROR, source_rid_, EPROTO);
        return;
    }
    const unsigned char type = reader.u8 ();
    const unsigned char flags = reader.u8 ();

    //  HELLO and ADMIT establish a transport. Every later frame must arrive
    //  on the exact physical connection currently selected for this RID;
    //  queued frames from a predecessor connection are stale after handover.
    const bool handshake = type == wire_hello || type == wire_admit;
    const bool transport_matches =
      handshake
        ? is_unselected_or_current_wire_transport (
            node_, source_rid_, connection_id_)
        : is_current_wire_transport (
            node_, source_rid_, connection_id_, type != wire_reject);
    if (!transport_matches) {
        close_frames (frames_);
        return;
    }

    switch (type) {
        case wire_hello:
        case wire_admit:
        case wire_update: {
            wire_descriptor_t descriptor;
            if (decode_descriptor (reader, &descriptor)) {
                if (type == wire_update)
                    handle_update (node_, source_rid_, descriptor);
                else
                    handle_hello_or_admit (
                      node_, source_rid_, descriptor, type == wire_hello,
                      connection_id_);
            }
            close_frames (frames_);
            return;
        }
        case wire_reject: {
            const uint32_t reason = reader.u32 ();
            if (!reader.failed)
                handle_reject (node_, source_rid_, reason);
            close_frames (frames_);
            return;
        }
        default:
            break;
    }

    //  Data messages: correlation for request/reply, channel name for
    //  channel kinds, spot addressing for spot kinds, then optional metadata
    //  frame and payload parts.
    uint64_t correlation = 0;
    int32_t terminal_result = 0;
    int32_t failure_errno = 0;
    std::string channel;
    std::string topic;
    rid_bytes_t source_spot_rid;
    rid_bytes_t target_spot_rid;
    uint64_t target_spot_generation = 0;
    if (type == wire_node_request || type == wire_channel_request || type == wire_spot_request
        || type == wire_reply || type == wire_bound_session_bind)
        correlation = reader.u64 ();
    if (type == wire_reply) {
        terminal_result = static_cast<int32_t> (reader.u32 ());
        failure_errno = static_cast<int32_t> (reader.u32 ());
    }
    if (type == wire_channel_send || type == wire_channel_request) {
        const size_t len = reader.u8 ();
        channel = reader.bytes (len);
    }
    if (type == wire_spot_send || type == wire_spot_request) {
        source_spot_rid = read_rid (reader);
        target_spot_rid = read_rid (reader);
        target_spot_generation = reader.u64 ();
    }
    instance_placement_value_t instance_placement;
    uint64_t instance_sender_generation = 0;
    rid_bytes_t instance_source_node_rid;
    zlink_instance_spot_operation_kind_t instance_operation_kind =
      ZLINK_INSTANCE_SPOT_OPERATION_INVALID;
    zlink_mesh_operation_id_t instance_operation_id;
    memset (&instance_operation_id, 0, sizeof (instance_operation_id));
    uint32_t instance_timeout_ms = 0;
    bool instance_redirected = false;
    uint64_t instance_redirect_spot_generation = 0;
    uint64_t instance_relay_serial = 0;
    if (type == wire_instance_spot) {
        if (!decode_instance_placement (reader, &instance_placement)) {
            reader.failed = true;
        } else {
            instance_sender_generation = reader.u64 ();
            instance_source_node_rid = read_rid (reader);
            source_spot_rid = read_rid (reader);
            instance_operation_kind =
              static_cast<zlink_instance_spot_operation_kind_t> (
                reader.u8 ());
            instance_operation_id.high = reader.u64 ();
            instance_operation_id.low = reader.u64 ();
            instance_timeout_ms = reader.u32 ();
            const unsigned char redirected = reader.u8 ();
            if (redirected > 1)
                reader.failed = true;
            instance_redirected = redirected != 0;
            instance_redirect_spot_generation = reader.u64 ();
            instance_relay_serial = reader.u64 ();

            const bool is_request =
              instance_operation_kind
              == ZLINK_INSTANCE_SPOT_OPERATION_REQUEST;
            const bool is_send =
              instance_operation_kind == ZLINK_INSTANCE_SPOT_OPERATION_SEND;
            if ((!is_request && !is_send)
                || instance_sender_generation == 0
                || instance_source_node_rid.empty ()
                || source_spot_rid.empty ()
                || reader.pos != reader.size
                || (flags & ~wire_flag_metadata) != 0
                || (!instance_redirected
                    && instance_source_node_rid != source_rid_)
                || (!instance_redirected && instance_relay_serial != 0)
                || (!instance_redirected
                    && instance_redirect_spot_generation != 0)
                || (instance_redirected
                    && instance_redirect_spot_generation == 0)
                || (is_request
                    && (instance_operation_id.high == 0
                        || instance_operation_id.low == 0
                        || (instance_redirected
                              ? instance_relay_serial == 0
                              : instance_operation_id.high
                                  != instance_sender_generation)))
                || (is_send
                    && (instance_operation_id.high != 0
                        || instance_operation_id.low != 0
                        || instance_timeout_ms != 0
                        || instance_relay_serial != 0)))
                reader.failed = true;
        }
    }
    if (type == wire_multicast) {
        const size_t channel_len = reader.u8 ();
        channel = reader.bytes (channel_len);
        const size_t topic_len = reader.u8 ();
        topic = reader.bytes (topic_len);
        source_spot_rid = read_rid (reader);
    }
    zlink_actor_ref_t source_actor;
    zlink_actor_ref_t target_actor;
    memset (&source_actor, 0, sizeof (source_actor));
    memset (&target_actor, 0, sizeof (target_actor));
    bool has_source_actor = false;
    bool join_entry = false;
    std::string actor_id;
    uint64_t left_prev_epoch = 0;
    uint64_t left_new_epoch = 0;
    uint64_t source_binding_generation = 0;
    uint64_t binding_generation = 0;
    uint64_t retired_binding_generation = 0;
    if (type == wire_actor_send || type == wire_actor_request) {
        if (type == wire_actor_request)
            correlation = reader.u64 ();
        const size_t source_len = reader.u8 ();
        if (source_len > 0) {
            const std::string id = reader.bytes (source_len);
            snprintf (source_actor.actor_id, sizeof (source_actor.actor_id), "%.*s",
                      (int) id.size (), id.data ());
            source_actor.generation = reader.u64 ();
            source_actor.node_rid = rid_value (source_rid_);
            has_source_actor = true;
        }
        target_actor = read_actor_ref (reader, node_->routing_id);
        if ((flags & wire_flag_source_spot_rid) != 0) {
            if ((flags & wire_flag_bound_session) == 0)
                reader.failed = true;
            else {
                source_spot_rid = read_rid (reader);
                source_binding_generation = reader.u64 ();
                if (source_binding_generation == 0)
                    reader.failed = true;
            }
        }
    }
    if (type == wire_bound_session_send) {
        target_actor = read_actor_ref (reader, source_rid_);
        target_actor.node_rid = rid_value (source_rid_);
        binding_generation = reader.u64 ();
    }
    if (type == wire_bound_session_bind) {
        target_actor = read_actor_ref (reader, source_rid_);
        binding_generation = reader.u64 ();
        retired_binding_generation = reader.u64 ();
        if ((binding_generation == 0) == (retired_binding_generation == 0))
            reader.failed = true;
    }
    if (type == wire_actor_lookup) {
        correlation = reader.u64 ();
        const size_t len = reader.u8 ();
        actor_id = reader.bytes (len);
    }
    if (type == wire_actor_destroy) {
        correlation = reader.u64 ();
        target_actor = read_actor_ref (reader, node_->routing_id);
    }
    if (type == wire_actor_join) {
        correlation = reader.u64 ();
        target_actor = read_actor_ref (reader, source_rid_);
        join_entry = reader.u8 () != 0;
        target_spot_rid = read_rid (reader);
        target_spot_generation = reader.u64 ();
    }
    if (type == wire_actor_left) {
        target_actor = read_actor_ref (reader, source_rid_);
        target_spot_rid = read_rid (reader);
        target_spot_generation = reader.u64 ();
        left_prev_epoch = reader.u64 ();
        left_new_epoch = reader.u64 ();
    }
    rid_bytes_t joined_previous_spot_rid;
    uint64_t joined_previous_spot_generation = 0;
    uint64_t joined_previous_membership_epoch = 0;
    uint64_t joined_current_membership_epoch = 0;
    if (type == wire_actor_joined) {
        target_actor = read_actor_ref (reader, source_rid_);
        joined_previous_spot_rid = read_rid (reader);
        joined_previous_spot_generation = reader.u64 ();
        joined_previous_membership_epoch = reader.u64 ();
        target_spot_rid = read_rid (reader);
        target_spot_generation = reader.u64 ();
        joined_current_membership_epoch = reader.u64 ();
    }
    zlink_actor_transfer_id_t transfer_id;
    memset (&transfer_id, 0, sizeof (transfer_id));
    uint64_t transfer_sequence = 0;
    uint64_t transfer_participant_id = 0;
    uint64_t transfer_final_sequence = 0;
    uint8_t transfer_role = 0;
    uint64_t transfer_offered_messages = 0;
    uint64_t transfer_offered_bytes = 0;
    bool transfer_seal_response = false;
    std::vector<transfer_participant_descriptor_t> transfer_participants;
    std::vector<transfer_participant_terminal_t> transfer_terminals;
    uint64_t relay_serial = 0;
    std::unique_ptr<queued_record_t> transfer_record;
    if (type == wire_transfer_ready) {
        transfer_id = read_transfer_id (reader);
        target_actor = read_actor_ref (reader, source_rid_);
        left_prev_epoch = reader.u64 (); //  expected epoch slot
        transfer_final_sequence = reader.u64 ();
        transfer_role = reader.u8 ();
        transfer_offered_messages = reader.u64 ();
        transfer_offered_bytes = reader.u64 ();
        const uint32_t participant_count = reader.u32 ();
        transfer_participants.reserve (participant_count);
        for (uint32_t i = 0; i < participant_count; ++i) {
            transfer_participant_descriptor_t participant;
            participant.participant_id = reader.u64 ();
            participant.binding_generation = reader.u64 ();
            participant.allowance_messages = reader.u64 ();
            participant.allowance_bytes = reader.u64 ();
            transfer_participants.push_back (participant);
        }
    }
    if (type == wire_transfer_data) {
        transfer_id = read_transfer_id (reader);
        transfer_participant_id = reader.u64 ();
        transfer_sequence = reader.u64 ();
        transfer_record.reset (new (std::nothrow) queued_record_t ());
        if (!transfer_record.get () || !read_record_header (reader, transfer_record.get (),
                                                            &relay_serial)) {
            close_frames (frames_);
            return;
        }
    }
    if (type == wire_transfer_ack) {
        transfer_id = read_transfer_id (reader);
        transfer_participant_id = reader.u64 ();
        transfer_sequence = reader.u64 ();
    }
    if (type == wire_transfer_seal) {
        transfer_id = read_transfer_id (reader);
        transfer_seal_response = reader.u8 () != 0;
        const uint32_t terminal_count = reader.u32 ();
        transfer_terminals.reserve (terminal_count);
        for (uint32_t i = 0; i < terminal_count; ++i) {
            transfer_participant_terminal_t terminal;
            terminal.participant_id = reader.u64 ();
            terminal.high_water = reader.u64 ();
            transfer_terminals.push_back (terminal);
        }
    }
    if (type == wire_transfer_complete)
        transfer_id = read_transfer_id (reader);
    if (type == wire_reply_relay) {
        relay_serial = reader.u64 ();
        terminal_result = static_cast<int32_t> (reader.u32 ());
        failure_errno = static_cast<int32_t> (reader.u32 ());
    }
    if (reader.failed
        || (type != wire_node_send && type != wire_node_request && type != wire_channel_send
            && type != wire_channel_request && type != wire_reply && type != wire_spot_send
            && type != wire_spot_request && type != wire_multicast && type != wire_actor_send
            && type != wire_actor_request && type != wire_actor_lookup
            && type != wire_actor_destroy && type != wire_actor_join && type != wire_actor_left
            && type != wire_actor_joined
            && type != wire_transfer_ready && type != wire_transfer_data
            && type != wire_transfer_ack && type != wire_reply_relay
            && type != wire_transfer_seal && type != wire_transfer_complete
            && type != wire_bound_session_send
            && type != wire_bound_session_bind
            && type != wire_instance_spot)) {
        if (type == wire_instance_spot)
            emit_peer_event (
              node_, ZLINK_MESH_MONITOR_PROTOCOL_ERROR, source_rid_, EPROTO);
        close_frames (frames_);
        return;
    }

    if (type == wire_instance_spot) {
        bool target_identity_matches = false;
        {
            std::lock_guard<std::mutex> lock (node_->mutex);
            peer_state_t *peer =
              find_peer_by_rid_locked (node_, source_rid_);
            target_identity_matches =
              peer && peer->state == ZLINK_MESH_PEER_ADMITTED
              && peer->lifecycle_generation == instance_sender_generation
              && instance_placement.node_rid == node_->routing_id
              && instance_placement.node_generation
                   == node_->lifecycle_generation;
        }
        if (!target_identity_matches) {
            close_frames (frames_);
            emit_peer_event (
              node_, ZLINK_MESH_MONITOR_PROTOCOL_ERROR, source_rid_, EPROTO);
            return;
        }
    }

    //  The reply tail is parsed after the envelope frame is closed below, so
    //  snapshot the remaining bytes while the frame storage is still alive.
    std::vector<unsigned char> reply_tail;
    if (type == wire_reply && reader.pos < reader.size)
        reply_tail.assign (reader.data + reader.pos, reader.data + reader.size);

    size_t next_frame = 1;
    std::vector<unsigned char> metadata;
    if (flags & wire_flag_metadata) {
        if (frames_->size () < 2) {
            close_frames (frames_);
            return;
        }
        zlink_msg_t &meta_frame = (*frames_)[1];
        const unsigned char *data =
          static_cast<const unsigned char *> (zlink_msg_data (&meta_frame));
        const size_t size = zlink_msg_size (&meta_frame);
        //  Invalid ingress metadata rejects the complete message before any
        //  mailbox admission.
        if (validate_metadata (data, size) != 0) {
            close_frames (frames_);
            emit_peer_event (node_, ZLINK_MESH_MONITOR_PROTOCOL_ERROR, source_rid_, EPROTO);
            return;
        }
        metadata.assign (data, data + size);
        next_frame = 2;
    }

    std::vector<zlink_msg_t> parts;
    for (size_t i = next_frame; i < frames_->size (); ++i)
        parts.push_back ((*frames_)[i]);
    //  Envelope/metadata frames are closed here; payload part ownership
    //  moved into parts.
    for (size_t i = 0; i < next_frame; ++i)
        zlink_msg_close (&(*frames_)[i]);
    frames_->clear ();

    if (type == wire_reply) {
        wire_reader_t tail_reader (reply_tail.empty () ? NULL : reply_tail.data (),
                                   reply_tail.size ());
        handle_reply (node_, source_rid_, correlation, terminal_result, failure_errno,
                      tail_reader, &parts);
        for (size_t i = 0; i < parts.size (); ++i)
            zlink_msg_close (&parts[i]);
        return;
    }
    if (type == wire_instance_spot) {
        if (parts.empty ()) {
            close_frames (&parts);
            emit_peer_event (
              node_, ZLINK_MESH_MONITOR_PROTOCOL_ERROR, source_rid_, EPROTO);
            return;
        }
        handle_instance_data (
          node_, source_rid_, instance_sender_generation,
          instance_source_node_rid, source_spot_rid, instance_placement,
          instance_operation_kind, instance_operation_id,
          instance_timeout_ms,
          instance_redirected, instance_redirect_spot_generation,
          instance_relay_serial, &metadata, &parts);
        return;
    }
    if (type == wire_actor_lookup) {
        close_frames (&parts);
        handle_actor_lookup (node_, source_rid_, correlation, actor_id);
        return;
    }
    if (type == wire_actor_destroy) {
        close_frames (&parts);
        handle_actor_destroy (node_, source_rid_, correlation, target_actor);
        return;
    }
    if (type == wire_actor_left) {
        close_frames (&parts);
        handle_actor_left (node_, source_rid_, target_actor, target_spot_rid,
                           target_spot_generation, left_prev_epoch, left_new_epoch);
        return;
    }
    if (type == wire_actor_joined) {
        close_frames (&parts);
        handle_actor_joined (
          node_, source_rid_, target_actor, joined_previous_spot_rid,
          joined_previous_spot_generation, joined_previous_membership_epoch,
          target_spot_rid, target_spot_generation,
          joined_current_membership_epoch);
        return;
    }
    if (type == wire_actor_join) {
        //  creation parts are optional for joins.
        handle_actor_join (node_, source_rid_, correlation, target_actor, join_entry,
                           target_spot_rid, target_spot_generation, &parts);
        return;
    }
    if (type == wire_transfer_ready) {
        close_frames (&parts);
        transfer_handle_ready (node_, source_rid_, transfer_id, target_actor, left_prev_epoch,
                               transfer_final_sequence, transfer_role,
                               transfer_offered_messages, transfer_offered_bytes,
                               transfer_participants);
        return;
    }
    if (type == wire_transfer_data) {
        transfer_record->parts = std::move (parts);
        for (size_t i = 0; i < transfer_record->parts.size (); ++i)
            transfer_record->byte_size += zlink_msg_size (&transfer_record->parts[i]);
        transfer_handle_data (node_, source_rid_, transfer_id, transfer_participant_id,
                              transfer_sequence, relay_serial, &transfer_record);
        return;
    }
    if (type == wire_transfer_ack) {
        close_frames (&parts);
        transfer_handle_ack (node_, source_rid_, transfer_id, transfer_participant_id,
                             transfer_sequence);
        return;
    }
    if (type == wire_transfer_seal) {
        close_frames (&parts);
        transfer_handle_seal (node_, source_rid_, transfer_id, transfer_seal_response,
                              transfer_terminals);
        return;
    }
    if (type == wire_transfer_complete) {
        close_frames (&parts);
        transfer_handle_complete (node_, source_rid_, transfer_id);
        return;
    }
    if (type == wire_reply_relay) {
        transfer_handle_reply_relay (node_, source_rid_, relay_serial, terminal_result,
                                     failure_errno, &parts);
        for (size_t i = 0; i < parts.size (); ++i)
            zlink_msg_close (&parts[i]);
        return;
    }
    if (type == wire_bound_session_bind) {
        int32_t terminal_result = ZLINK_REQUEST_OK;
        int32_t failure_errno = 0;
        uint64_t membership_epoch = 0;
        {
            std::lock_guard<std::mutex> lock (node_->mutex);
            peer_state_t *peer =
              find_peer_by_rid_locked (node_, source_rid_);
            if (!peer || peer->state != ZLINK_MESH_PEER_ADMITTED)
                return;
            const std::string actor_id (target_actor.actor_id);
            if (binding_generation == 0) {
                const std::map<std::string, bound_session_route_t>::iterator route =
                  node_->bound_session_routes.find (actor_id);
                if (route != node_->bound_session_routes.end ()
                    && route->second.actor_generation
                         == target_actor.generation
                    && route->second.binding_generation
                         == retired_binding_generation
                    && route->second.source_node_rid == source_rid_)
                    node_->bound_session_routes.erase (route);

                const std::map<std::string, uint64_t>::const_iterator active =
                  node_->active_transfer_by_actor.find (actor_id);
                if (active != node_->active_transfer_by_actor.end ()) {
                    const auto stored = node_->transfers.find (active->second);
                    transfer_state_t *transfer =
                      stored == node_->transfers.end () ? NULL : &stored->second;
                    if (transfer
                        && transfer->role == ZLINK_ACTOR_TRANSFER_TARGET
                        && transfer->actor.generation
                             == target_actor.generation
                        && transfer->peer_node_rid == source_rid_
                        && transfer->has_pending_bound_session_route
                        && transfer->pending_bound_session_route.actor_generation
                             == target_actor.generation
                        && transfer->pending_bound_session_route.binding_generation
                             == retired_binding_generation
                        && transfer->pending_bound_session_route.source_node_rid
                             == source_rid_)
                        transfer->has_pending_bound_session_route = false;
                }
            } else {
                const std::map<std::string, actor_state_t>::const_iterator actor =
                  node_->actors.find (actor_id);
                if (actor == node_->actors.end ()) {
                    terminal_result = ZLINK_REQUEST_NOT_FOUND;
                    failure_errno = ENOENT;
                } else if (actor->second.generation
                             != target_actor.generation) {
                    terminal_result = ZLINK_REQUEST_CONFLICT;
                    failure_errno = ESTALE;
                } else if (actor->second.draining && correlation == 0) {
                    const std::map<std::string, uint64_t>::const_iterator active =
                      node_->active_transfer_by_actor.find (actor_id);
                    transfer_state_t *transfer = NULL;
                    if (active != node_->active_transfer_by_actor.end ()) {
                        const auto stored = node_->transfers.find (active->second);
                        if (stored != node_->transfers.end ())
                            transfer = &stored->second;
                    }
                    if (!transfer
                        || transfer->role != ZLINK_ACTOR_TRANSFER_TARGET
                        || transfer->actor.generation
                             != target_actor.generation
                        || transfer->peer_node_rid != source_rid_
                        || (transfer->phase != ZLINK_ACTOR_TRANSFER_PREPARING
                            && transfer->phase
                                 != ZLINK_ACTOR_TRANSFER_COMMITTED)) {
                        terminal_result = ZLINK_REQUEST_CONFLICT;
                        failure_errno = ESTALE;
                    } else {
                        bound_session_route_t route;
                        route.actor_generation = target_actor.generation;
                        route.binding_generation = binding_generation;
                        route.source_node_rid = source_rid_;
                        transfer->pending_bound_session_route = route;
                        transfer->has_pending_bound_session_route = true;
                        membership_epoch = actor->second.membership_epoch;
                    }
                } else if (actor->second.draining) {
                    terminal_result = ZLINK_REQUEST_CONFLICT;
                    failure_errno = ESTALE;
                } else {
                    membership_epoch = actor->second.membership_epoch;
                    bound_session_route_t route;
                    route.actor_generation = target_actor.generation;
                    route.binding_generation = binding_generation;
                    route.source_node_rid = source_rid_;
                    node_->bound_session_routes[actor_id] = route;
                }
            }
        }
        if (correlation != 0)
            wire_submit_bound_session_bind_reply (
              node_, source_rid_, correlation, terminal_result,
              failure_errno, binding_generation, membership_epoch);
        return;
    }
    if (parts.empty ()) {
        //  Data messages carry at least one payload part.
        return;
    }
    if (type == wire_bound_session_send) {
        {
            std::lock_guard<std::mutex> lock (node_->mutex);
            peer_state_t *peer =
              find_peer_by_rid_locked (node_, source_rid_);
            if (!peer || peer->state != ZLINK_MESH_PEER_ADMITTED) {
                close_frames (&parts);
                return;
            }
        }
        (void) stream_sessions_deliver_bound_session (
          node_, target_actor, binding_generation, &parts);
        close_frames (&parts);
        return;
    }
    if (type == wire_actor_send || type == wire_actor_request) {
        handle_actor_data (node_, source_rid_, source_spot_rid,
                           source_binding_generation,
                           type == wire_actor_request, correlation, source_actor,
                           has_source_actor, target_actor, &metadata, &parts);
        return;
    }
    if (type == wire_spot_send || type == wire_spot_request) {
        handle_spot_data (node_, source_rid_, type == wire_spot_request, correlation,
                          source_spot_rid, target_spot_rid, target_spot_generation, &metadata,
                          &parts);
        return;
    }
    if (type == wire_multicast) {
        handle_multicast (node_, source_rid_, channel, topic, source_spot_rid, &metadata, &parts);
        return;
    }
    handle_data (node_, source_rid_, type, correlation, channel, &metadata, &parts);
}

void drain_monitor (mesh_node_t *node_)
{
    if (!node_->router_monitor)
        return;
    for (;;) {
        zlink_socket_monitor_event_t event;
        uint64_t connection_id = 0;
        uint32_t internal_flags = 0;
        memset (&event, 0, sizeof (event));
        if (recv_socket_monitor_event_internal (
              node_->router_monitor, &event, &connection_id, &internal_flags,
              ZLINK_RECV_FLAGS_DONTWAIT)
            != 0)
            return;
        if (event.event == ZLINK_EVENT_CONNECTION_READY) {
            //  Aggregate ready-count updates are not new physical transports.
            //  Only the private edge marker may start admission.
            if ((internal_flags
                 & socket_monitor_internal_connection_ready_edge)
                == 0)
                continue;
            //  Outbound connects match a connecting intent by endpoint; the
            //  handshake starts with our HELLO to the revealed peer rid.
            bool ours = false;
            bool transport_handover = false;
            bool readiness_changed = false;
            rid_bytes_t ready_rid;
            {
                std::lock_guard<std::mutex> connection_lock (
                  node_->peer_connection_mutex);
                std::lock_guard<std::mutex> lock (node_->mutex);
                ready_rid = rid_bytes (event.routing_id);
                peer_state_t *intent =
                  find_intent_by_endpoint_locked (node_, event.remote_addr);
                ours = intent && event.routing_id.size > 0;
                peer_state_t *ready_peer = NULL;
                bool selected_transport = !ready_rid.empty ();
                if (!ready_rid.empty ()) {
                    const peer_transport_t candidate (
                      connection_id, ours);
                    const std::map<rid_bytes_t, peer_transport_t>::iterator
                      current =
                        node_->current_peer_transports.find (ready_rid);
                    if (current != node_->current_peer_transports.end ()
                        && current->second.direction_known
                        && current->second.locally_initiated != ours) {
                        //  Reciprocal connectors briefly expose both
                        //  directions. ROUTER selects the same direction on
                        //  both peers from their stable RIDs; preserve that
                        //  choice even if monitor delivery for the losing
                        //  pipe arrives later.
                        const bool locally_initiated_wins =
                          node_->routing_id < ready_rid;
                        selected_transport =
                          ours == locally_initiated_wins;
                    }
                    if (selected_transport)
                        node_->current_peer_transports[ready_rid] =
                          candidate;
                    ready_peer =
                      find_peer_by_rid_locked (node_, ready_rid);
                }
                if (selected_transport
                    && ready_peer && intent && ready_peer != intent) {
                    //  Reciprocal connectors can make the selected physical
                    //  pipe visible by RID after an inbound discovery row was
                    //  admitted but before the configured outbound intent was
                    //  associated with that RID. Keep one logical row: the
                    //  configured intent owns readiness and its public intent
                    //  id, while the discovery observation contributes its
                    //  admitted descriptor and MIXED source.
                    if (ready_peer->source == ZLINK_MESH_PEER_DISCOVERY
                        || ready_peer->source == ZLINK_MESH_PEER_MIXED)
                        intent->source = ZLINK_MESH_PEER_MIXED;
                    intent->rid = ready_rid;
                    if (intent->lifecycle_generation == 0
                        || ready_peer->lifecycle_generation
                             >= intent->lifecycle_generation) {
                        intent->lifecycle_generation =
                          ready_peer->lifecycle_generation;
                        intent->descriptor_revision =
                          ready_peer->descriptor_revision;
                        intent->channels = ready_peer->channels;
                    }
                    ready_peer->state = ZLINK_MESH_PEER_CLOSED;
                    ready_peer->transport = peer_transport_t ();
                    ready_peer->last_changed_ms = now_ms ();
                    ready_peer = intent;
                    readiness_changed = true;
                }
                if (selected_transport && ready_peer) {
                    //  Monitor edges are drained before the corresponding
                    //  ROUTER frame is dispatched. A deterministic
                    //  direction change must therefore enter CONNECTING and
                    //  let that pipe's HELLO/ADMIT complete the handover; an
                    //  eager rebind would classify the pending HELLO as a
                    //  duplicate lifetime.
                    const bool replaced =
                      apply_transport_ready_locked (
                        ready_peer, connection_id);
                    transport_handover =
                      replaced || transport_handover;
                    readiness_changed =
                      replaced || readiness_changed;
                }
                if (selected_transport && intent && event.routing_id.size > 0) {
                    intent->rid = rid_bytes (event.routing_id);
                    const zlink_mesh_peer_state_t previous_state =
                      intent->state;
                    if (intent != ready_peer)
                        transport_handover =
                          apply_transport_ready_locked (
                            intent, connection_id)
                          || transport_handover;
                    if (intent->state != previous_state)
                        readiness_changed = true;
                }
                if (readiness_changed)
                    recompute_readiness_locked (node_);
            }
            if (transport_handover)
                emit_peer_event (
                  node_, ZLINK_MESH_MONITOR_PEER_CLOSED,
                  ready_rid, ENOTCONN);
            if (ours) {
                std::vector<unsigned char> frame = make_envelope (wire_hello, 0);
                {
                    std::lock_guard<std::mutex> lock (node_->mutex);
                    encode_descriptor_locked (node_, frame);
                }
                send_control (node_, event.routing_id, frame);
            }
        } else if (event.event == ZLINK_EVENT_DISCONNECTED) {
            if (event.routing_id.size > 0)
                handle_peer_down (node_, rid_bytes (event.routing_id),
                                  connection_id);
            else if (event.value == ZLINK_DISCONNECT_HANDSHAKE_FAILED)
                //  A transport handshake can fail before a routing id
                //  exists. The socket's terminal reason is the single
                //  non-duplicating source for this pre-admission rejection.
                //  Keep peer_rid zero-valued as required by the monitor ABI.
                emit_peer_event (node_, ZLINK_MESH_MONITOR_PEER_REJECTED,
                                 rid_bytes_t (), EPROTO);
        }
    }
}

void catch_up_transport_monitor (mesh_node_t *node_,
                                 const rid_bytes_t &source_rid_,
                                 uint64_t frame_connection_id_)
{
    if (source_rid_.empty () || frame_connection_id_ == 0)
        return;
    //  The engine publishes READY before ROUTER exposes application frames,
    //  but the monitor PAIR can become readable a few scheduler turns after
    //  the data pipe. Connection ids are process-local and monotonic. Wait a
    //  short bounded interval until the selected transport has reached this
    //  pipe (or a newer reconnect) so a cross-direction HELLO is not compared
    //  with the predecessor's admission state.
    for (int waited = 0; waited < ingress_poll_ms; ++waited) {
        drain_monitor (node_);
        {
            std::lock_guard<std::mutex> lock (node_->mutex);
            const std::map<rid_bytes_t, peer_transport_t>::const_iterator
              current =
                node_->current_peer_transports.find (source_rid_);
            if (current != node_->current_peer_transports.end ()
                && current->second.connection_id >= frame_connection_id_)
                return;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    drain_monitor (node_);
}

void run_ingress_loop (mesh_node_t *node_)
{
    while (!node_->io_stop.load (std::memory_order_acquire)) {
        drain_monitor (node_);

        zlink_pollitem_t item;
        memset (&item, 0, sizeof (item));
        item.socket = node_->router_socket;
        item.events = ZLINK_POLLIN;
        const int rc = zlink_poll (&item, 1, ingress_poll_ms, NULL);
        if (rc < 0) {
            //  Timeout surfaces as EAGAIN from the poller; both it and EINTR
            //  just re-arm the loop.
            if (errno == EAGAIN || errno == EINTR)
                continue;
            return;
        }
        if (rc == 0)
            continue;
        for (;;) {
            //  An escaping exception would terminate the process from this
            //  detached thread; under allocation pressure the inbound
            //  message is dropped, which is the same observable outcome as
            //  losing it in transit.
            try {
                rid_bytes_t source;
                uint64_t connection_id = 0;
                std::vector<zlink_msg_t> frames;
                if (!recv_wire_message (
                      node_, &source, &connection_id, &frames))
                    break;
                catch_up_transport_monitor (
                  node_, source, connection_id);
                dispatch_wire_message (
                  node_, source, connection_id, &frames);
            }
            catch (const std::bad_alloc &) {
                continue;
            }
        }
    }
}
}
}
