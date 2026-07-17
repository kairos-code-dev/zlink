/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/mesh/mesh_c_internal.hpp"
#include "services/mesh/mesh_wire.hpp"

#include "api/socket/request_reply_runtime_core.hpp"
#include "api/socket/request_timeout_scheduler_internal.hpp"
#include "utils/err.hpp"
#include "utils/macros.hpp"

#include <string.h>

using namespace zlink::mesh;

namespace
{
//  --- shared submit plumbing --------------------------------------------------

struct operation_timeout_ctx_t
{
    mesh_node_t *node;
    uint64_t operation_low;
};

void on_operation_timeout (void *userdata_)
{
    std::unique_ptr<operation_timeout_ctx_t> ctx (
      static_cast<operation_timeout_ctx_t *> (userdata_));
    if (!ctx.get ())
        return;
    mesh_node_t *node = as_mesh_node (ctx->node);
    if (!node)
        return;
    pending_operation_t op;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        std::unordered_map<uint64_t, pending_operation_t>::iterator it =
          node->operations.find (ctx->operation_low);
        if (it == node->operations.end ())
            return;
        op = it->second;
        node->operations.erase (it);
    }
    complete_operation (node, op, ZLINK_REQUEST_TIMED_OUT, ETIMEDOUT, NULL, NULL);
}


//  Validates borrowed input parts and copies them (reference counted) into
//  record storage.
int copy_borrowed_parts (const zlink_msg_t *parts_, size_t part_count_, queued_record_t *record_)
{
    record_->parts.resize (part_count_);
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_init (&record_->parts[i]);
    for (size_t i = 0; i < part_count_; ++i) {
        if (zlink_msg_copy (&record_->parts[i], const_cast<zlink_msg_t *> (&parts_[i])) != 0) {
            errno = EFAULT;
            return -1;
        }
        record_->byte_size += zlink_msg_size (&record_->parts[i]);
    }
    return 0;
}

int check_submit_input (const zlink_mesh_metadata_view_t *metadata_,
                        const zlink_msg_t *parts_,
                        size_t part_count_)
{
    if (!parts_ || part_count_ == 0) {
        errno = EINVAL;
        return -1;
    }
    if (metadata_) {
        if (!metadata_->data || metadata_->size == 0) {
            errno = EINVAL;
            return -1;
        }
        if (validate_metadata (metadata_->data, metadata_->size) != 0)
            return -1;
    }
    return 0;
}

//  Selection: a channel's candidates are the admitted positive-weight remote
//  peers plus the local node when it is a ready member of the channel. Phase
//  A carries no admitted remote pipes, so remote candidates resolve later.
struct channel_target_t
{
    bool is_local;
    size_t peer_index;
};

int select_channel_target_locked (mesh_node_t *node_,
                                  const std::string &channel_,
                                  channel_target_t *target_out_)
{
    std::vector<channel_target_t> candidates;
    for (size_t i = 0; i < node_->peers.size (); ++i) {
        const peer_state_t &peer = node_->peers[i];
        if (peer.state != ZLINK_MESH_PEER_ADMITTED)
            continue;
        std::map<std::string, uint32_t>::const_iterator it = peer.channels.find (channel_);
        if (it == peer.channels.end () || it->second == 0)
            continue;
        channel_target_t candidate;
        candidate.is_local = false;
        candidate.peer_index = i;
        candidates.push_back (candidate);
    }
    std::map<std::string, uint32_t>::const_iterator local_it = node_->channels.find (channel_);
    if (local_it != node_->channels.end () && local_it->second > 0
        && node_->state == ZLINK_MESH_NODE_READY) {
        channel_target_t candidate;
        candidate.is_local = true;
        candidate.peer_index = 0;
        candidates.push_back (candidate);
    }
    if (candidates.empty ()) {
        errno = ENOENT;
        return -1;
    }
    size_t &cursor = node_->rr_cursor[channel_];
    *target_out_ = candidates[cursor % candidates.size ()];
    cursor = (cursor + 1) % candidates.size ();
    return 0;
}

//  Common state gate for submits: arguments were already validated.
int check_submit_state_locked (mesh_node_t *node_)
{
    if (node_->state == ZLINK_MESH_NODE_CREATED) {
        errno = EINVAL;
        return -1;
    }
    if (node_->state == ZLINK_MESH_NODE_DRAINING || node_->state == ZLINK_MESH_NODE_STOPPED) {
        errno = ESHUTDOWN;
        return -1;
    }
    return 0;
}

//  Builds and admits one locally-delivered record. Handles request
//  bookkeeping (operation + one-shot reply route) when operation_kind_ is a
//  request kind.
//  Submit observability: one MESSAGE_SUBMITTED per successful application
//  submit and one BACKPRESSURED per admission rejection under pressure. The
//  monitor queue aggregates both kinds under overflow.
void emit_submit_event (mesh_node_t *node_,
                        zlink_mesh_monitor_event_kind_t kind_,
                        zlink_mesh_owner_kind_t owner_kind_,
                        const std::string &channel_,
                        int32_t error_)
{
    zlink_mesh_monitor_event_t event;
    memset (&event, 0, sizeof (event));
    event.kind = kind_;
    event.owner_kind = owner_kind_;
    if (!channel_.empty ())
        snprintf (event.channel_name, sizeof (event.channel_name), "%s", channel_.c_str ());
    event.failure_errno = error_;
    emit_monitor_event (node_, event);
}

zlink_submit_result_t submit_local_record (mesh_node_t *node_,
                                           const owner_id_t &destination_,
                                           const owner_id_t &requester_,
                                           zlink_mesh_record_kind_t kind_,
                                           const rid_bytes_t &source_spot_rid_,
                                           const std::string &channel_name_,
                                           const zlink_mesh_metadata_view_t *metadata_,
                                           const zlink_msg_t *parts_,
                                           size_t part_count_,
                                           zlink_mesh_operation_id_t *operation_id_out_,
                                           zlink_mesh_operation_kind_t operation_kind_,
                                           zlink_send_flags_t flags_,
                                           uint32_t timeout_ms_)
{
    const bool is_request = operation_id_out_ != NULL;

    std::unique_ptr<queued_record_t> record (new (std::nothrow) queued_record_t ());
    if (!record.get ()) {
        errno = ENOMEM;
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    }
    record->kind = kind_;
    record->source_node_rid = node_->routing_id;
    record->source_spot_rid = source_spot_rid_;
    record->channel_name = channel_name_;
    if (metadata_) {
        record->has_metadata = true;
        record->application_metadata.assign (metadata_->data, metadata_->data + metadata_->size);
        record->byte_size += metadata_->size;
    }
    if (copy_borrowed_parts (parts_, part_count_, record.get ()) != 0)
        return ZLINK_SUBMIT_INTERNAL_ERROR;

    zlink_mesh_operation_id_t op_id;
    memset (&op_id, 0, sizeof (op_id));
    uint64_t reply_serial = 0;
    if (is_request) {
        op_id = register_operation (node_, operation_kind_, requester_, timeout_ms_);
        std::lock_guard<std::mutex> lock (node_->mutex);
        reply_serial = node_->next_reply_serial++;
        reply_route_t route;
        route.kind = reply_route_t::kind_generic;
        route.requester = requester_;
        route.requester_node_generation = node_->lifecycle_generation;
        route.operation_id = op_id;
        route.operation_kind = operation_kind_;
        route.consumed = false;
        memset (&route.join_actor, 0, sizeof (route.join_actor));
        route.join_target_spot_generation = 0;
        node_->reply_routes[reply_serial] = route;

        record->operation_id = op_id;
        record->operation_kind = operation_kind_;
        record->has_reply_token = true;
        seal_reply_token (node_, reply_serial, &record->reply_token);
    }

    const bool blocking = (flags_ & ZLINK_SEND_FLAGS_DONTWAIT) == 0;
    const uint32_t admit_timeout =
      blocking ? static_cast<uint32_t> (node_->sndtimeo_ms < 0 ? 0 : node_->sndtimeo_ms) : 0;
    if (admit_record (node_, destination_, domain_application, record, blocking, admit_timeout)
        != 0) {
        const int reason = errno;
        if (is_request) {
            std::lock_guard<std::mutex> lock (node_->mutex);
            node_->operations.erase (op_id.low);
            node_->reply_routes.erase (reply_serial);
        }
        errno = reason;
        return submit_errno_result ();
    }

    if (is_request) {
        if (schedule_operation_timeout (node_, op_id.low, timeout_ms_) != 0) {
            //  Timeout scheduling failure leaves the operation pending
            //  without a deadline; surface the internal failure.
            return ZLINK_SUBMIT_INTERNAL_ERROR;
        }
        *operation_id_out_ = op_id;
    }

    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        if (node_->monitor)
            node_->monitor->counters.submitted_messages += 1;
    }
    emit_submit_event (node_, ZLINK_MESH_MONITOR_MESSAGE_SUBMITTED,
                       static_cast<zlink_mesh_owner_kind_t> (destination_.kind), channel_name_,
                       0);
    return ZLINK_SUBMIT_OK;
}

//  Remote submit to an admitted peer: request bookkeeping mirrors the local
//  path but the reply returns over the wire keyed by the operation serial.
zlink_submit_result_t submit_remote (mesh_node_t *node_,
                                     const rid_bytes_t &peer_rid_,
                                     const owner_id_t &requester_,
                                     bool channel_kind_,
                                     const std::string &channel_,
                                     const zlink_mesh_metadata_view_t *metadata_,
                                     const zlink_msg_t *parts_,
                                     size_t part_count_,
                                     zlink_mesh_operation_id_t *operation_id_out_,
                                     zlink_mesh_operation_kind_t operation_kind_,
                                     zlink_send_flags_t flags_,
                                     uint32_t timeout_ms_)
{
    const bool is_request = operation_id_out_ != NULL;
    if (!is_request) {
        const wire_type_t type = channel_kind_ ? wire_channel_send : wire_node_send;
        const zlink_submit_result_t rc = wire_submit_data (
          node_, peer_rid_, type, 0, channel_, metadata_, parts_, part_count_, flags_);
        if (rc == ZLINK_SUBMIT_OK) {
            {
                std::lock_guard<std::mutex> lock (node_->mutex);
                if (node_->monitor)
                    node_->monitor->counters.submitted_messages += 1;
            }
            emit_submit_event (node_, ZLINK_MESH_MONITOR_MESSAGE_SUBMITTED,
                               ZLINK_MESH_OWNER_NODE, channel_, 0);
        } else if (errno == EAGAIN || errno == ETIMEDOUT) {
            const int reason = errno;
            emit_submit_event (node_, ZLINK_MESH_MONITOR_BACKPRESSURED, ZLINK_MESH_OWNER_NODE,
                               channel_, reason);
            errno = reason;
        }
        return rc;
    }

    const zlink_mesh_operation_id_t op_id =
      register_operation (node_, operation_kind_, requester_, timeout_ms_);
    const wire_type_t type = channel_kind_ ? wire_channel_request : wire_node_request;
    const zlink_submit_result_t rc = wire_submit_data (
      node_, peer_rid_, type, op_id.low, channel_, metadata_, parts_, part_count_, flags_);
    if (rc != ZLINK_SUBMIT_OK) {
        const int reason = errno;
        {
            std::lock_guard<std::mutex> lock (node_->mutex);
            node_->operations.erase (op_id.low);
        }
        if (reason == EAGAIN || reason == ETIMEDOUT)
            emit_submit_event (node_, ZLINK_MESH_MONITOR_BACKPRESSURED, ZLINK_MESH_OWNER_NODE,
                               channel_, reason);
        errno = reason;
        return rc;
    }
    if (schedule_operation_timeout (node_, op_id.low, timeout_ms_) != 0)
        return ZLINK_SUBMIT_INTERNAL_ERROR;
    *operation_id_out_ = op_id;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        if (node_->monitor)
            node_->monitor->counters.submitted_messages += 1;
    }
    emit_submit_event (node_, ZLINK_MESH_MONITOR_MESSAGE_SUBMITTED, ZLINK_MESH_OWNER_NODE,
                       channel_, 0);
    return ZLINK_SUBMIT_OK;
}

//  Shared body of the four node/channel submit entry points.
zlink_submit_result_t node_channel_submit (void *mesh_node_,
                                           const zlink_routing_id_t *target_rid_,
                                           const char *channel_name_,
                                           const owner_id_t *requester_,
                                           const rid_bytes_t *source_spot_rid_,
                                           const zlink_mesh_metadata_view_t *metadata_,
                                           const zlink_msg_t *parts_,
                                           size_t part_count_,
                                           zlink_mesh_operation_id_t *operation_id_out_,
                                           zlink_mesh_operation_kind_t operation_kind_,
                                           zlink_send_flags_t flags_,
                                           uint32_t timeout_ms_)
{
    mesh_node_t *node = as_mesh_node (mesh_node_);
    if (!node) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (check_submit_input (metadata_, parts_, part_count_) != 0)
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    if (operation_id_out_)
        memset (operation_id_out_, 0, sizeof (*operation_id_out_));

    const owner_id_t requester = requester_ ? *requester_ : node_owner ();
    const rid_bytes_t source_spot = source_spot_rid_ ? *source_spot_rid_ : rid_bytes_t ();

    if (target_rid_) {
        //  Direct node submit: needs an admitted pipe for the target RID.
        if (target_rid_->size == 0) {
            errno = EINVAL;
            return ZLINK_SUBMIT_INVALID_ARGUMENT;
        }
        bool admitted = false;
        rid_bytes_t rid;
        {
            std::unique_lock<std::mutex> lock (node->mutex);
            if (check_submit_state_locked (node) != 0)
                return submit_errno_result ();
            rid = rid_bytes (*target_rid_);
            for (size_t i = 0; i < node->peers.size (); ++i) {
                if (node->peers[i].state == ZLINK_MESH_PEER_ADMITTED
                    && node->peers[i].rid == rid) {
                    admitted = true;
                    break;
                }
            }
        }
        if (!admitted) {
            errno = ENOTCONN;
            return ZLINK_SUBMIT_NOT_CONNECTED;
        }
        return submit_remote (node, rid, requester, false, std::string (), metadata_, parts_,
                              part_count_, operation_id_out_, operation_kind_, flags_,
                              timeout_ms_);
    }

    std::string channel;
    if (check_name (channel_name_, ZLINK_CHANNEL_NAME_MAX, &channel) != 0)
        return ZLINK_SUBMIT_INVALID_ARGUMENT;

    channel_target_t target;
    rid_bytes_t remote_rid;
    {
        std::unique_lock<std::mutex> lock (node->mutex);
        if (check_submit_state_locked (node) != 0)
            return submit_errno_result ();
        if (select_channel_target_locked (node, channel, &target) != 0)
            return ZLINK_SUBMIT_NOT_FOUND;
        if (!target.is_local)
            remote_rid = node->peers[target.peer_index].rid;
    }

    if (!target.is_local) {
        return submit_remote (node, remote_rid, requester, true, channel, metadata_, parts_,
                              part_count_, operation_id_out_,
                              operation_id_out_ ? ZLINK_MESH_OPERATION_CHANNEL_REQUEST
                                                : operation_kind_,
                              flags_, timeout_ms_);
    }

    const zlink_mesh_record_kind_t kind = operation_id_out_
                                            ? ZLINK_MESH_RECORD_CHANNEL_REQUEST
                                            : ZLINK_MESH_RECORD_CHANNEL_SEND;
    return submit_local_record (node, node_owner (), requester, kind, source_spot, channel,
                                metadata_, parts_, part_count_, operation_id_out_, operation_kind_,
                                flags_, timeout_ms_);
}
}

//  --- MeshNode node/channel messaging ------------------------------------------

zlink_submit_result_t zlink_mesh_node_send_to_node (void *mesh_node_,
                                                    const zlink_routing_id_t *target_rid_,
                                                    const zlink_mesh_metadata_view_t *metadata_,
                                                    const zlink_msg_t *parts_,
                                                    size_t part_count_,
                                                    zlink_send_flags_t flags_)
{
    if (!target_rid_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    return node_channel_submit (mesh_node_, target_rid_, NULL, NULL, NULL, metadata_, parts_,
                                part_count_, NULL, static_cast<zlink_mesh_operation_kind_t> (0),
                                flags_, 0);
}

zlink_submit_result_t zlink_mesh_node_request_to_node (void *mesh_node_,
                                                       const zlink_routing_id_t *target_rid_,
                                                       const zlink_mesh_metadata_view_t *metadata_,
                                                       const zlink_msg_t *parts_,
                                                       size_t part_count_,
                                                       zlink_mesh_operation_id_t *operation_id_out_,
                                                       zlink_send_flags_t flags_,
                                                       uint32_t timeout_ms_)
{
    if (!target_rid_ || !operation_id_out_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    return node_channel_submit (mesh_node_, target_rid_, NULL, NULL, NULL, metadata_, parts_,
                                part_count_, operation_id_out_, ZLINK_MESH_OPERATION_NODE_REQUEST,
                                flags_, timeout_ms_);
}

zlink_submit_result_t zlink_mesh_node_send_to_channel (void *mesh_node_,
                                                       const char *channel_name_,
                                                       const zlink_mesh_metadata_view_t *metadata_,
                                                       const zlink_msg_t *parts_,
                                                       size_t part_count_,
                                                       zlink_send_flags_t flags_)
{
    return node_channel_submit (mesh_node_, NULL, channel_name_, NULL, NULL, metadata_, parts_,
                                part_count_, NULL, static_cast<zlink_mesh_operation_kind_t> (0),
                                flags_, 0);
}

zlink_submit_result_t
zlink_mesh_node_request_to_channel (void *mesh_node_,
                                    const char *channel_name_,
                                    const zlink_mesh_metadata_view_t *metadata_,
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
    return node_channel_submit (mesh_node_, NULL, channel_name_, NULL, NULL, metadata_, parts_,
                                part_count_, operation_id_out_,
                                ZLINK_MESH_OPERATION_CHANNEL_REQUEST, flags_, timeout_ms_);
}

//  --- Spot messaging -------------------------------------------------------------

namespace
{
//  Resolves a live facade and its owner id, or fails with the right errno.
int resolve_facade (void *spot_,
                    spot_facade_t **facade_out_,
                    mesh_node_t **node_out_,
                    owner_id_t *owner_out_)
{
    spot_facade_t *facade = as_spot_facade (spot_);
    if (!facade) {
        errno = EFAULT;
        return -1;
    }
    *facade_out_ = facade;
    *node_out_ = facade->node;
    if (owner_out_)
        *owner_out_ = spot_owner (facade->spot_rid, facade->generation);
    return 0;
}
}

zlink_submit_result_t zlink_spot_send_to_channel (void *spot_,
                                                  const char *channel_name_,
                                                  const zlink_mesh_metadata_view_t *metadata_,
                                                  const zlink_msg_t *parts_,
                                                  size_t part_count_,
                                                  zlink_send_flags_t flags_)
{
    spot_facade_t *facade;
    mesh_node_t *node;
    owner_id_t owner;
    if (resolve_facade (spot_, &facade, &node, &owner) != 0)
        return ZLINK_SUBMIT_INVALID_HANDLE;
    return node_channel_submit (node, NULL, channel_name_, &owner, &facade->spot_rid, metadata_,
                                parts_, part_count_, NULL,
                                static_cast<zlink_mesh_operation_kind_t> (0), flags_, 0);
}

zlink_submit_result_t zlink_spot_request_to_channel (void *spot_,
                                                     const char *channel_name_,
                                                     const zlink_mesh_metadata_view_t *metadata_,
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
    spot_facade_t *facade;
    mesh_node_t *node;
    owner_id_t owner;
    if (resolve_facade (spot_, &facade, &node, &owner) != 0)
        return ZLINK_SUBMIT_INVALID_HANDLE;
    return node_channel_submit (node, NULL, channel_name_, &owner, &facade->spot_rid, metadata_,
                                parts_, part_count_, operation_id_out_,
                                ZLINK_MESH_OPERATION_SPOT_REQUEST, flags_, timeout_ms_);
}

namespace
{
zlink_submit_result_t spot_direct_submit (void *spot_,
                                          const zlink_routing_id_t *target_node_rid_,
                                          const zlink_routing_id_t *target_spot_rid_,
                                          uint64_t target_spot_generation_,
                                          const zlink_mesh_metadata_view_t *metadata_,
                                          const zlink_msg_t *parts_,
                                          size_t part_count_,
                                          zlink_mesh_operation_id_t *operation_id_out_,
                                          zlink_send_flags_t flags_,
                                          uint32_t timeout_ms_)
{
    spot_facade_t *facade;
    mesh_node_t *node;
    owner_id_t requester;
    if (resolve_facade (spot_, &facade, &node, &requester) != 0)
        return ZLINK_SUBMIT_INVALID_HANDLE;
    if (!target_node_rid_ || target_node_rid_->size == 0 || !target_spot_rid_
        || target_spot_rid_->size == 0 || target_spot_generation_ == 0) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (check_submit_input (metadata_, parts_, part_count_) != 0)
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    if (operation_id_out_)
        memset (operation_id_out_, 0, sizeof (*operation_id_out_));

    const rid_bytes_t target_node = rid_bytes (*target_node_rid_);
    const rid_bytes_t target_spot = rid_bytes (*target_spot_rid_);

    owner_id_t destination;
    bool local_target_found = false;
    bool generation_conflict = false;
    bool remote_target = false;
    {
        std::unique_lock<std::mutex> lock (node->mutex);
        if (check_submit_state_locked (node) != 0)
            return submit_errno_result ();
        if (target_node != node->routing_id) {
            //  Remote node targets require an admitted pipe.
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
            remote_target = true;
        }
        if (!remote_target) {
            const std::string key (target_spot.begin (), target_spot.end ());
            std::map<std::string, spot_state_t>::iterator it = node->spots.find (key);
            if (it != node->spots.end ()) {
                if (it->second.generation == target_spot_generation_) {
                    local_target_found = true;
                    destination = spot_owner (target_spot, it->second.generation);
                } else {
                    generation_conflict = true;
                }
            }
        }
    }

    const bool is_request = operation_id_out_ != NULL;
    if (remote_target) {
        if (!is_request) {
            const zlink_submit_result_t rc =
              wire_submit_spot (node, target_node, false, 0, facade->spot_rid, target_spot,
                                target_spot_generation_, metadata_, parts_, part_count_, flags_);
            return rc;
        }
        const zlink_mesh_operation_id_t op_id = register_operation (
          node, ZLINK_MESH_OPERATION_SPOT_REQUEST, requester, timeout_ms_);
        const zlink_submit_result_t rc =
          wire_submit_spot (node, target_node, true, op_id.low, facade->spot_rid, target_spot,
                            target_spot_generation_, metadata_, parts_, part_count_, flags_);
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
    if (!local_target_found) {
        if (!is_request) {
            //  One-way send: submit succeeds, remote/local absence is not
            //  reported back per the Spot contract.
            return ZLINK_SUBMIT_OK;
        }
        //  Request: admission succeeded, the terminal completion carries the
        //  lookup failure.
        const zlink_mesh_operation_id_t op_id = register_operation (
          node, ZLINK_MESH_OPERATION_SPOT_REQUEST, requester, timeout_ms_);
        *operation_id_out_ = op_id;
        pending_operation_t op;
        {
            std::lock_guard<std::mutex> lock (node->mutex);
            std::unordered_map<uint64_t, pending_operation_t>::iterator op_it =
              node->operations.find (op_id.low);
            op = op_it->second;
            node->operations.erase (op_it);
        }
        if (generation_conflict)
            complete_operation (node, op, ZLINK_REQUEST_CONFLICT, ESTALE, NULL, NULL);
        else
            complete_operation (node, op, ZLINK_REQUEST_NOT_FOUND, ENOENT, NULL, NULL);
        return ZLINK_SUBMIT_OK;
    }

    return submit_local_record (node, destination, requester,
                                is_request ? ZLINK_MESH_RECORD_SPOT_REQUEST
                                           : ZLINK_MESH_RECORD_SPOT_SEND,
                                facade->spot_rid, std::string (), metadata_, parts_, part_count_,
                                operation_id_out_, ZLINK_MESH_OPERATION_SPOT_REQUEST, flags_,
                                timeout_ms_);
}
}

zlink_submit_result_t zlink_spot_send_to_spot (void *spot_,
                                               const zlink_routing_id_t *target_node_rid_,
                                               const zlink_routing_id_t *target_spot_rid_,
                                               uint64_t target_spot_generation_,
                                               const zlink_mesh_metadata_view_t *metadata_,
                                               const zlink_msg_t *parts_,
                                               size_t part_count_,
                                               zlink_send_flags_t flags_)
{
    return spot_direct_submit (spot_, target_node_rid_, target_spot_rid_, target_spot_generation_,
                               metadata_, parts_, part_count_, NULL, flags_, 0);
}

zlink_submit_result_t zlink_spot_request_to_spot (void *spot_,
                                                  const zlink_routing_id_t *target_node_rid_,
                                                  const zlink_routing_id_t *target_spot_rid_,
                                                  uint64_t target_spot_generation_,
                                                  const zlink_mesh_metadata_view_t *metadata_,
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
    return spot_direct_submit (spot_, target_node_rid_, target_spot_rid_, target_spot_generation_,
                               metadata_, parts_, part_count_, operation_id_out_, flags_,
                               timeout_ms_);
}

//  --- Logical Multicast ------------------------------------------------------------

namespace
{
bool subscription_matches (const spot_state_t &spot_,
                           const std::string &channel_,
                           const std::string &topic_)
{
    for (std::set<subscription_key_t>::const_iterator it = spot_.subscriptions.begin ();
         it != spot_.subscriptions.end (); ++it) {
        if (it->channel != channel_)
            continue;
        if (it->kind == ZLINK_SPOT_SUBSCRIPTION_EXACT) {
            if (it->filter == topic_)
                return true;
        } else {
            if (topic_.compare (0, it->filter.size (), it->filter) == 0)
                return true;
        }
    }
    return false;
}

zlink_submit_result_t publish_common (mesh_node_t *node_,
                                      const rid_bytes_t *source_spot_rid_,
                                      int nodrop_,
                                      const char *channel_name_,
                                      const char *topic_,
                                      const zlink_mesh_metadata_view_t *metadata_,
                                      const zlink_msg_t *parts_,
                                      size_t part_count_,
                                      zlink_mesh_publish_detail_t *detail_out_,
                                      zlink_send_flags_t flags_)
{
    std::string channel;
    if (check_name (channel_name_, ZLINK_CHANNEL_NAME_MAX, &channel) != 0)
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    std::string topic;
    if (check_name (topic_, ZLINK_MESH_TOPIC_MAX, &topic) != 0)
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    if (!valid_utf8 (reinterpret_cast<const unsigned char *> (topic.data ()), topic.size ()))
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    if (check_submit_input (metadata_, parts_, part_count_) != 0)
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    if (detail_out_ && check_versioned (detail_out_) != 0)
        return ZLINK_SUBMIT_INVALID_ARGUMENT;

    //  Snapshot local Spot matches and admitted remote channel members.
    std::vector<owner_id_t> local_targets;
    std::vector<rid_bytes_t> remote_targets;
    {
        std::unique_lock<std::mutex> lock (node_->mutex);
        if (check_submit_state_locked (node_) != 0)
            return submit_errno_result ();
        const bool local_member = node_->channels.count (channel) > 0;
        if (local_member) {
            for (std::map<std::string, spot_state_t>::iterator it = node_->spots.begin ();
                 it != node_->spots.end (); ++it) {
                if (!it->second.draining && subscription_matches (it->second, channel, topic))
                    local_targets.push_back (spot_owner (it->second.rid, it->second.generation));
            }
        }
        for (size_t i = 0; i < node_->peers.size (); ++i) {
            const peer_state_t &peer = node_->peers[i];
            if (peer.state != ZLINK_MESH_PEER_ADMITTED)
                continue;
            std::map<std::string, uint32_t>::const_iterator ch = peer.channels.find (channel);
            if (ch != peer.channels.end () && ch->second > 0)
                remote_targets.push_back (peer.rid);
        }
    }
    const uint32_t snapshot_remote = static_cast<uint32_t> (remote_targets.size ());
    const uint32_t snapshot_local = static_cast<uint32_t> (local_targets.size ());
    if (snapshot_remote + snapshot_local == 0) {
        errno = ENOENT;
        return ZLINK_SUBMIT_NOT_FOUND;
    }

    //  Delivery. NODROP=1 delivers to every snapshot target (local mailbox
    //  reservations plus remote pipe reservations) or to none: both checks
    //  run under the node mutex, and the wire probe-and-commit serializes on
    //  the wire send mutex so the reservation is atomic against concurrent
    //  submits from this node.
    uint32_t admitted_local = 0;
    uint32_t dropped_local = 0;
    uint32_t admitted_remote = 0;
    uint32_t dropped_remote = 0;
    uint32_t unreachable_remote = 0;
    const bool blocking = (flags_ & ZLINK_SEND_FLAGS_DONTWAIT) == 0;
    const uint64_t reserve_deadline =
      blocking && node_->sndtimeo_ms > 0
        ? now_ms () + static_cast<uint64_t> (node_->sndtimeo_ms)
        : 0;
retry_reserve:
    admitted_local = 0;
    dropped_local = 0;
    admitted_remote = 0;
    dropped_remote = 0;
    unreachable_remote = 0;
    {
        std::unique_lock<std::mutex> lock (node_->mutex);
        const uint64_t message_budget = node_->effective_message_budget ();
        const uint64_t byte_budget = node_->effective_byte_budget ();
        size_t payload_bytes = 0;
        for (size_t i = 0; i < part_count_; ++i)
            payload_bytes += zlink_msg_size (const_cast<zlink_msg_t *> (&parts_[i]));

        std::vector<mailbox_t *> accepting;
        std::vector<owner_id_t> accepting_ids;
        for (size_t t = 0; t < local_targets.size (); ++t) {
            std::map<owner_id_t, owner_state_t>::iterator it = node_->owners.find (local_targets[t]);
            if (it == node_->owners.end ()) {
                ++dropped_local;
                continue;
            }
            mailbox_t &mailbox = it->second.domains[domain_application];
            const bool fits = mailbox.pending_messages + 1 <= message_budget
                              && mailbox.pending_bytes + payload_bytes <= byte_budget;
            if (fits) {
                accepting.push_back (&mailbox);
                accepting_ids.push_back (local_targets[t]);
            } else {
                ++dropped_local;
            }
        }

        if (nodrop_ && dropped_local > 0) {
            if (blocking && reserve_deadline != 0 && now_ms () < reserve_deadline) {
                //  A NODROP blocking publish waits for capacity within
                //  SNDTIMEO; claim releases signal the condition.
                node_->cv.wait_for (lock, std::chrono::milliseconds (
                                            std::min<uint64_t> (10, reserve_deadline - now_ms ())));
                lock.unlock ();
                goto retry_reserve;
            }
            errno = (flags_ & ZLINK_SEND_FLAGS_DONTWAIT) ? EAGAIN : ETIMEDOUT;
            return ZLINK_SUBMIT_BACKPRESSURED;
        }

        //  Remote leg before the local commit: a NODROP remote failure must
        //  leave the local mailboxes untouched. The wire reserve commits
        //  nothing on backpressure, so a blocking publish may retry the whole
        //  reserve within SNDTIMEO.
        const zlink_submit_result_t remote_rc = wire_publish_remote_locked (
          node_, remote_targets, channel, topic,
          source_spot_rid_ ? *source_spot_rid_ : rid_bytes_t (), nodrop_, metadata_, parts_,
          part_count_, &admitted_remote, &dropped_remote, &unreachable_remote);
        if (nodrop_ && remote_rc != ZLINK_SUBMIT_OK) {
            if (blocking && reserve_deadline != 0 && now_ms () < reserve_deadline) {
                node_->cv.wait_for (lock, std::chrono::milliseconds (
                                            std::min<uint64_t> (10, reserve_deadline - now_ms ())));
                lock.unlock ();
                goto retry_reserve;
            }
            errno = (flags_ & ZLINK_SEND_FLAGS_DONTWAIT) ? EAGAIN : ETIMEDOUT;
            return ZLINK_SUBMIT_BACKPRESSURED;
        }

        for (size_t t = 0; t < accepting.size (); ++t) {
            std::unique_ptr<queued_record_t> record (new (std::nothrow) queued_record_t ());
            if (!record.get ()) {
                errno = ENOMEM;
                return ZLINK_SUBMIT_OUT_OF_MEMORY;
            }
            record->kind = ZLINK_MESH_RECORD_SPOT_MULTICAST;
            record->source_node_rid = node_->routing_id;
            if (source_spot_rid_)
                record->source_spot_rid = *source_spot_rid_;
            record->channel_name = channel;
            record->topic = topic;
            if (metadata_) {
                record->has_metadata = true;
                record->application_metadata.assign (metadata_->data,
                                                     metadata_->data + metadata_->size);
                record->byte_size += metadata_->size;
            }
            if (copy_borrowed_parts (parts_, part_count_, record.get ()) != 0)
                return ZLINK_SUBMIT_INTERNAL_ERROR;

            accepting[t]->pending_messages += 1;
            accepting[t]->pending_bytes += record->byte_size;
            accepting[t]->records.push_back (std::move (record));
            node_->ready.insert (
              std::make_pair (accepting_ids[t], static_cast<int> (domain_application)));
            ++admitted_local;
        }
        node_->cv.notify_all ();
        if (node_->monitor) {
            node_->monitor->counters.multicast_messages += 1;
            node_->monitor->counters.multicast_dropped_targets += dropped_local + dropped_remote;
        }
    }
    //  Wake any registered ready handler after releasing the mutex.
    if (admitted_local > 0 && !local_targets.empty ())
        signal_ready (node_, local_targets[0], domain_application);

    if (detail_out_) {
        init_versioned (detail_out_);
        detail_out_->snapshot_remote_target_count = snapshot_remote - unreachable_remote;
        detail_out_->admitted_remote_target_count = admitted_remote;
        detail_out_->dropped_remote_target_count = dropped_remote;
        detail_out_->snapshot_local_spot_count = snapshot_local;
        detail_out_->admitted_local_spot_count = admitted_local;
        detail_out_->dropped_local_spot_count = dropped_local;
    }

    zlink_mesh_monitor_event_t event;
    memset (&event, 0, sizeof (event));
    event.kind = (dropped_local > 0 || dropped_remote > 0)
                   ? ZLINK_MESH_MONITOR_MULTICAST_DROPPED
                   : ZLINK_MESH_MONITOR_MULTICAST_COMMITTED;
    event.snapshot_remote_target_count = snapshot_remote - unreachable_remote;
    event.admitted_remote_target_count = admitted_remote;
    event.dropped_remote_target_count = dropped_remote;
    event.snapshot_local_spot_count = snapshot_local;
    event.admitted_local_spot_count = admitted_local;
    event.dropped_local_spot_count = dropped_local;
    snprintf (event.channel_name, sizeof (event.channel_name), "%s", channel.c_str ());
    emit_monitor_event (node_, event);
    return ZLINK_SUBMIT_OK;
}
}

namespace zlink
{
namespace mesh
{
int schedule_operation_timeout (mesh_node_t *node_, uint64_t operation_low_, uint32_t timeout_ms_)
{
    if (timeout_ms_ == 0)
        return 0;
    std::shared_ptr<zlink::request_timeout::task_t> task;
    return zlink::request_reply_runtime::schedule_timeout_task<operation_timeout_ctx_t> (
      timeout_ms_, &on_operation_timeout,
      [&] (operation_timeout_ctx_t &ctx_) {
          ctx_.node = node_;
          ctx_.operation_low = operation_low_;
      },
      &task);
}

}
}

zlink_submit_result_t zlink_mesh_node_publisher_publish (void *publisher_,
                                                         const char *channel_name_,
                                                         const char *topic_,
                                                         const zlink_mesh_metadata_view_t *metadata_,
                                                         const zlink_msg_t *parts_,
                                                         size_t part_count_,
                                                         zlink_mesh_publish_detail_t *detail_out_,
                                                         zlink_send_flags_t flags_)
{
    publisher_t *pub = as_publisher (publisher_);
    if (!pub) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    return publish_common (pub->node, NULL, pub->nodrop, channel_name_, topic_, metadata_, parts_,
                           part_count_, detail_out_, flags_);
}

zlink_submit_result_t zlink_spot_publish (void *spot_,
                                          const char *channel_name_,
                                          const char *topic_,
                                          const zlink_mesh_metadata_view_t *metadata_,
                                          const zlink_msg_t *parts_,
                                          size_t part_count_,
                                          zlink_mesh_publish_detail_t *detail_out_,
                                          zlink_send_flags_t flags_)
{
    spot_facade_t *facade;
    mesh_node_t *node;
    if (resolve_facade (spot_, &facade, &node, NULL) != 0)
        return ZLINK_SUBMIT_INVALID_HANDLE;
    int nodrop = 1;
    {
        std::lock_guard<std::mutex> lock (node->mutex);
        const std::string key (facade->spot_rid.begin (), facade->spot_rid.end ());
        std::map<std::string, spot_state_t>::iterator it = node->spots.find (key);
        if (it == node->spots.end () || it->second.generation != facade->generation) {
            errno = ESTALE;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        nodrop = it->second.publish_nodrop;
    }
    return publish_common (node, &facade->spot_rid, nodrop, channel_name_, topic_, metadata_,
                           parts_, part_count_, detail_out_, flags_);
}

//  --- local subscription --------------------------------------------------------

namespace
{
zlink_config_result_t subscription_common (void *spot_,
                                           const char *channel_name_,
                                           const char *topic_filter_,
                                           zlink_spot_subscription_kind_t kind_,
                                           bool set_)
{
    spot_facade_t *facade = as_spot_facade (spot_);
    if (!facade) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (kind_ != ZLINK_SPOT_SUBSCRIPTION_EXACT && kind_ != ZLINK_SPOT_SUBSCRIPTION_PREFIX) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    std::string channel;
    if (check_name (channel_name_, ZLINK_CHANNEL_NAME_MAX, &channel) != 0)
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    if (!topic_filter_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    const std::string filter (topic_filter_);
    if (filter.size () > ZLINK_MESH_TOPIC_MAX) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    if (!filter.empty ()
        && !valid_utf8 (reinterpret_cast<const unsigned char *> (filter.data ()),
                        filter.size ())) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }

    mesh_node_t *node = facade->node;
    std::lock_guard<std::mutex> lock (node->mutex);
    const std::string key (facade->spot_rid.begin (), facade->spot_rid.end ());
    std::map<std::string, spot_state_t>::iterator it = node->spots.find (key);
    if (it == node->spots.end () || it->second.generation != facade->generation) {
        errno = ESTALE;
        return ZLINK_CONFIG_INVALID_STATE;
    }
    subscription_key_t sub;
    sub.channel = channel;
    sub.filter = filter;
    sub.kind = kind_;
    if (set_)
        it->second.subscriptions.insert (sub);
    else
        it->second.subscriptions.erase (sub);
    return ZLINK_CONFIG_OK;
}
}

zlink_config_result_t zlink_spot_set_subscription (void *spot_,
                                                   const char *channel_name_,
                                                   const char *topic_filter_,
                                                   zlink_spot_subscription_kind_t kind_)
{
    return subscription_common (spot_, channel_name_, topic_filter_, kind_, true);
}

zlink_config_result_t zlink_spot_unset_subscription (void *spot_,
                                                     const char *channel_name_,
                                                     const char *topic_filter_,
                                                     zlink_spot_subscription_kind_t kind_)
{
    return subscription_common (spot_, channel_name_, topic_filter_, kind_, false);
}
