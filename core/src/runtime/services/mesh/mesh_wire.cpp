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

//  Transport lifecycle and outbound submits: the node-owned ROUTER, its
//  monitor, endpoint resolution and every wire_submit_* egress path.
namespace zlink
{
namespace mesh
{
//  --- frame transmission -------------------------------------------------------

int send_frame (mesh_node_t *node_,
                const zlink_routing_id_t &target_,
                const std::vector<unsigned char> &bytes_,
                zlink_part_flag_t part_flag_,
                zlink_send_flags_t flags_)
{
    zlink_msg_t frame;
    if (zlink_msg_init_size (&frame, bytes_.size ()) != 0)
        return -1;
    memcpy (zlink_msg_data (&frame), bytes_.data (), bytes_.size ());
    const zlink_submit_result_t rc =
      zlink_send_part_rid (node_->router_socket, &target_, &frame, flags_, part_flag_);
    if (rc != ZLINK_SUBMIT_OK) {
        const int saved_errno = errno;
        zlink_msg_close (&frame);
        errno = saved_errno;
        return -1;
    }
    return 0;
}

//  Sends one control message (single frame) to target_ without blocking.
int send_control (mesh_node_t *node_,
                  const zlink_routing_id_t &target_,
                  const std::vector<unsigned char> &frame_)
{
    std::lock_guard<std::mutex> lock (node_->wire_send_mutex);
    return send_frame (node_, target_, frame_, ZLINK_PART_FINAL, ZLINK_SEND_FLAGS_DONTWAIT);
}

//  send_data_message body for callers that already hold wire_send_mutex.
zlink_submit_result_t send_data_message_unlocked (mesh_node_t *node_,
                                                  const zlink_routing_id_t &target_,
                                                  const std::vector<unsigned char> &envelope_,
                                                  const std::vector<unsigned char> *metadata_,
                                                  const zlink_msg_t *parts_,
                                                  size_t part_count_,
                                                  zlink_send_flags_t flags_);

//  Sends envelope [+ metadata] + payload parts. Payload parts are borrowed
//  from the caller and copied (reference counted) per part.
zlink_submit_result_t send_data_message (mesh_node_t *node_,
                                         const zlink_routing_id_t &target_,
                                         const std::vector<unsigned char> &envelope_,
                                         const std::vector<unsigned char> *metadata_,
                                         const zlink_msg_t *parts_,
                                         size_t part_count_,
                                         zlink_send_flags_t flags_)
{
    std::lock_guard<std::mutex> lock (node_->wire_send_mutex);
    return send_data_message_unlocked (node_, target_, envelope_, metadata_, parts_, part_count_,
                                       flags_);
}

zlink_submit_result_t send_data_message_unlocked (mesh_node_t *node_,
                                                  const zlink_routing_id_t &target_,
                                                  const std::vector<unsigned char> &envelope_,
                                                  const std::vector<unsigned char> *metadata_,
                                                  const zlink_msg_t *parts_,
                                                  size_t part_count_,
                                                  zlink_send_flags_t flags_)
{
    const bool has_payload = part_count_ > 0;
    if (send_frame (node_, target_, envelope_,
                    (metadata_ || has_payload) ? ZLINK_PART_MORE : ZLINK_PART_FINAL, flags_)
        != 0)
        return submit_errno_result ();
    if (metadata_) {
        if (send_frame (node_, target_, *metadata_,
                        has_payload ? ZLINK_PART_MORE : ZLINK_PART_FINAL, flags_)
            != 0)
            return submit_errno_result ();
    }
    for (size_t i = 0; i < part_count_; ++i) {
        zlink_msg_t copy;
        if (zlink_msg_init (&copy) != 0)
            return ZLINK_SUBMIT_OUT_OF_MEMORY;
        if (zlink_msg_copy (&copy, const_cast<zlink_msg_t *> (&parts_[i])) != 0) {
            zlink_msg_close (&copy);
            errno = EFAULT;
            return ZLINK_SUBMIT_INTERNAL_ERROR;
        }
        const zlink_part_flag_t part_flag =
          (i + 1 < part_count_) ? ZLINK_PART_MORE : ZLINK_PART_FINAL;
        const zlink_submit_result_t rc =
          zlink_send_part_rid (node_->router_socket, &target_, &copy, flags_, part_flag);
        if (rc != ZLINK_SUBMIT_OK) {
            const int saved_errno = errno;
            zlink_msg_close (&copy);
            errno = saved_errno;
            return submit_errno_result ();
        }
    }
    return ZLINK_SUBMIT_OK;
}

//  --- lifecycle ---------------------------------------------------------------------

int wire_start (mesh_node_t *node_)
{
    void *router = zlink_socket (node_->ctx, ZLINK_SOCKET_ROUTER);
    if (!router)
        return -1;

    zlink_routing_id_t rid = rid_value (node_->routing_id);
    if (zlink_set_routing_id (router, reinterpret_cast<const char *> (rid.data), rid.size)
        != ZLINK_CONFIG_OK) {
        zlink_close (router);
        return -1;
    }
    if (node_->router_hwm_override > 0) {
        const int hwm = node_->router_hwm_override;
        (void) zlink_set_option (router, ZLINK_OPT_SNDHWM, &hwm, sizeof (hwm));
        (void) zlink_set_option (router, ZLINK_OPT_RCVHWM, &hwm, sizeof (hwm));
    }
    if (node_->sndtimeo_ms >= 0) {
        (void) zlink_set_option (router, ZLINK_OPT_SNDTIMEO, &node_->sndtimeo_ms,
                                 sizeof (node_->sndtimeo_ms));
    }
    if (zlink_bind (router, node_->bind_endpoint.c_str ()) != ZLINK_BIND_OK) {
        const int saved_errno = errno;
        zlink_close (router);
        errno = saved_errno;
        return -1;
    }
    char resolved[512] = "";
    size_t resolved_size = sizeof (resolved);
    if (zlink_get_option (router, ZLINK_OPT_LAST_ENDPOINT, resolved, &resolved_size)
          == ZLINK_CONFIG_OK
        && resolved[0] != '\0')
        node_->bind_endpoint = resolved;

    zlink_socket_monitor_open_options_t monitor_options;
    memset (&monitor_options, 0, sizeof (monitor_options));
    monitor_options.events =
      ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED;
    node_->router_monitor = zlink_socket_monitor_open (router, &monitor_options);

    node_->router_socket = router;
    node_->io_stop.store (false, std::memory_order_release);
    try {
        node_->io_thread = std::thread (run_ingress_loop, node_);
    } catch (...) {
        if (node_->router_monitor)
            zlink_monitor_close (&node_->router_monitor);
        zlink_close (router);
        node_->router_socket = NULL;
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

void wire_stop (mesh_node_t *node_)
{
    node_->io_stop.store (true, std::memory_order_release);
    if (node_->io_thread.joinable ())
        node_->io_thread.join ();
    if (node_->router_monitor)
        zlink_monitor_close (&node_->router_monitor);
    if (node_->router_socket) {
        zlink_close (node_->router_socket);
        node_->router_socket = NULL;
    }
}

int wire_connect_endpoint (mesh_node_t *node_, const std::string &endpoint_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return -1;
    }
    if (zlink_connect (node_->router_socket, endpoint_.c_str ()) != ZLINK_CONNECT_OK)
        return -1;
    return 0;
}

void wire_broadcast_update_locked (mesh_node_t *node_)
{
    if (!node_->router_socket)
        return;
    std::vector<unsigned char> frame = make_envelope (wire_update, 0);
    encode_descriptor_locked (node_, frame);
    for (size_t i = 0; i < node_->peers.size (); ++i) {
        if (node_->peers[i].state != ZLINK_MESH_PEER_ADMITTED)
            continue;
        const zlink_routing_id_t target = rid_value (node_->peers[i].rid);
        send_control (node_, target, frame);
    }
}

zlink_submit_result_t wire_submit_data (mesh_node_t *node_,
                                        const rid_bytes_t &peer_rid_,
                                        wire_type_t type_,
                                        uint64_t correlation_,
                                        const std::string &channel_,
                                        const zlink_mesh_metadata_view_t *metadata_,
                                        const zlink_msg_t *parts_,
                                        size_t part_count_,
                                        zlink_send_flags_t flags_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope =
      make_envelope (static_cast<unsigned char> (type_), metadata_ ? wire_flag_metadata : 0);
    if (type_ == wire_node_request || type_ == wire_channel_request)
        put_u64 (envelope, correlation_);
    if (type_ == wire_channel_send || type_ == wire_channel_request) {
        put_u8 (envelope, static_cast<unsigned char> (channel_.size ()));
        put_bytes (envelope, channel_.data (), channel_.size ());
    }
    std::vector<unsigned char> metadata;
    if (metadata_)
        metadata.assign (metadata_->data, metadata_->data + metadata_->size);

    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, metadata_ ? &metadata : NULL, parts_,
                              part_count_, flags_);
}

zlink_submit_result_t wire_submit_spot (mesh_node_t *node_,
                                        const rid_bytes_t &peer_rid_,
                                        bool is_request_,
                                        uint64_t correlation_,
                                        const rid_bytes_t &source_spot_rid_,
                                        const rid_bytes_t &target_spot_rid_,
                                        uint64_t target_spot_generation_,
                                        const zlink_mesh_metadata_view_t *metadata_,
                                        const zlink_msg_t *parts_,
                                        size_t part_count_,
                                        zlink_send_flags_t flags_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope =
      make_envelope (static_cast<unsigned char> (is_request_ ? wire_spot_request : wire_spot_send),
                     metadata_ ? wire_flag_metadata : 0);
    if (is_request_)
        put_u64 (envelope, correlation_);
    put_rid (envelope, source_spot_rid_);
    put_rid (envelope, target_spot_rid_);
    put_u64 (envelope, target_spot_generation_);
    std::vector<unsigned char> metadata;
    if (metadata_)
        metadata.assign (metadata_->data, metadata_->data + metadata_->size);

    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, metadata_ ? &metadata : NULL, parts_,
                              part_count_, flags_);
}

zlink_submit_result_t wire_publish_remote_locked (mesh_node_t *node_,
                                                  const std::vector<rid_bytes_t> &targets_,
                                                  const std::string &channel_,
                                                  const std::string &topic_,
                                                  const rid_bytes_t &source_spot_rid_,
                                                  int nodrop_,
                                                  const zlink_mesh_metadata_view_t *metadata_,
                                                  const zlink_msg_t *parts_,
                                                  size_t part_count_,
                                                  uint32_t *admitted_out_,
                                                  uint32_t *dropped_out_)
{
    *admitted_out_ = 0;
    *dropped_out_ = 0;
    if (targets_.empty ())
        return ZLINK_SUBMIT_OK;
    if (!node_->router_socket) {
        *dropped_out_ = static_cast<uint32_t> (targets_.size ());
        if (nodrop_) {
            errno = EAGAIN;
            return ZLINK_SUBMIT_BACKPRESSURED;
        }
        return ZLINK_SUBMIT_OK;
    }

    std::vector<unsigned char> envelope =
      make_envelope (wire_multicast, metadata_ ? wire_flag_metadata : 0);
    put_u8 (envelope, static_cast<unsigned char> (channel_.size ()));
    put_bytes (envelope, channel_.data (), channel_.size ());
    put_u8 (envelope, static_cast<unsigned char> (topic_.size ()));
    put_bytes (envelope, topic_.data (), topic_.size ());
    put_rid (envelope, source_spot_rid_);
    std::vector<unsigned char> metadata;
    if (metadata_)
        metadata.assign (metadata_->data, metadata_->data + metadata_->size);

    //  All outbound wire traffic serializes on wire_send_mutex, so this
    //  reserve-then-commit is atomic against other submits from this node.
    std::lock_guard<std::mutex> lock (node_->wire_send_mutex);
    if (nodrop_) {
        //  Reserve phase: every target pipe must have capacity before any
        //  target receives a frame (all-or-none). A capacity miss commits
        //  nothing so the caller may safely retry the whole reserve.
        zlink::socket_base_t *router = try_as_socket (node_->router_socket);
        if (!router) {
            *dropped_out_ = static_cast<uint32_t> (targets_.size ());
            errno = EAGAIN;
            return ZLINK_SUBMIT_BACKPRESSURED;
        }
        for (size_t i = 0; i < targets_.size (); ++i) {
            const zlink_routing_id_t target = rid_value (targets_[i]);
            if (!router->routed_target_writable (&target)) {
                *dropped_out_ = static_cast<uint32_t> (targets_.size ());
                errno = EAGAIN;
                return ZLINK_SUBMIT_BACKPRESSURED;
            }
        }
    }
    //  Commit phase. With the reserve held under wire_send_mutex a capacity
    //  failure cannot occur here; a target that still fails lost its pipe
    //  (peer disconnect), which drops that target without breaking the
    //  admission atomicity of the surviving set.
    for (size_t i = 0; i < targets_.size (); ++i) {
        const zlink_routing_id_t target = rid_value (targets_[i]);
        const zlink_submit_result_t rc =
          send_data_message_unlocked (node_, target, envelope, metadata_ ? &metadata : NULL,
                                      parts_, part_count_, ZLINK_SEND_FLAGS_DONTWAIT);
        if (rc == ZLINK_SUBMIT_OK)
            *admitted_out_ += 1;
        else
            *dropped_out_ += 1;
    }
    if (nodrop_ && *dropped_out_ > 0 && *admitted_out_ == 0) {
        errno = EAGAIN;
        return ZLINK_SUBMIT_BACKPRESSURED;
    }
    return ZLINK_SUBMIT_OK;
}

zlink_submit_result_t wire_submit_actor_data (mesh_node_t *node_,
                                              const rid_bytes_t &peer_rid_,
                                              bool is_request_,
                                              uint64_t correlation_,
                                              const zlink_actor_ref_t *source_actor_,
                                              const zlink_actor_ref_t &target_actor_,
                                              const zlink_mesh_metadata_view_t *metadata_,
                                              const zlink_msg_t *parts_,
                                              size_t part_count_,
                                              zlink_send_flags_t flags_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (
      static_cast<unsigned char> (is_request_ ? wire_actor_request : wire_actor_send),
      metadata_ ? wire_flag_metadata : 0);
    if (is_request_)
        put_u64 (envelope, correlation_);
    if (source_actor_) {
        put_actor_ref (envelope, *source_actor_);
    } else {
        put_u8 (envelope, 0);
    }
    put_actor_ref (envelope, target_actor_);
    std::vector<unsigned char> metadata;
    if (metadata_)
        metadata.assign (metadata_->data, metadata_->data + metadata_->size);

    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, metadata_ ? &metadata : NULL, parts_,
                              part_count_, flags_);
}

zlink_submit_result_t wire_submit_actor_lookup (mesh_node_t *node_,
                                                const rid_bytes_t &peer_rid_,
                                                uint64_t correlation_,
                                                const std::string &actor_id_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (wire_actor_lookup, 0);
    put_u64 (envelope, correlation_);
    put_u8 (envelope, static_cast<unsigned char> (actor_id_.size ()));
    put_bytes (envelope, actor_id_.data (), actor_id_.size ());
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_actor_destroy (mesh_node_t *node_,
                                                 const rid_bytes_t &peer_rid_,
                                                 uint64_t correlation_,
                                                 const zlink_actor_ref_t &actor_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (wire_actor_destroy, 0);
    put_u64 (envelope, correlation_);
    put_actor_ref (envelope, actor_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_actor_join (mesh_node_t *node_,
                                              const rid_bytes_t &peer_rid_,
                                              uint64_t correlation_,
                                              const zlink_actor_ref_t &actor_,
                                              bool entry_,
                                              const rid_bytes_t &target_spot_rid_,
                                              uint64_t target_spot_generation_,
                                              const zlink_msg_t *creation_parts_,
                                              size_t creation_part_count_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (wire_actor_join, 0);
    put_u64 (envelope, correlation_);
    put_actor_ref (envelope, actor_);
    put_u8 (envelope, entry_ ? 1 : 0);
    put_rid (envelope, target_spot_rid_);
    put_u64 (envelope, target_spot_generation_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, creation_parts_,
                              creation_part_count_, ZLINK_SEND_FLAGS_NONE);
}

void wire_notify_actor_left (mesh_node_t *node_,
                             const rid_bytes_t &peer_rid_,
                             const zlink_actor_ref_t &actor_,
                             const rid_bytes_t &previous_spot_rid_,
                             uint64_t previous_spot_generation_,
                             uint64_t previous_membership_epoch_,
                             uint64_t current_membership_epoch_)
{
    if (!node_->router_socket)
        return;
    std::vector<unsigned char> envelope = make_envelope (wire_actor_left, 0);
    put_actor_ref (envelope, actor_);
    put_rid (envelope, previous_spot_rid_);
    put_u64 (envelope, previous_spot_generation_);
    put_u64 (envelope, previous_membership_epoch_);
    put_u64 (envelope, current_membership_epoch_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    send_control (node_, target, envelope);
}

zlink_submit_result_t wire_submit_join_reply (mesh_node_t *node_,
                                              const rid_bytes_t &peer_rid_,
                                              uint64_t correlation_,
                                              uint32_t join_result_,
                                              const rid_bytes_t &spot_rid_,
                                              uint64_t spot_generation_,
                                              const zlink_msg_t *parts_,
                                              size_t part_count_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (wire_reply, 0);
    put_u64 (envelope, correlation_);
    put_u32 (envelope, static_cast<uint32_t> (join_result_ == ZLINK_ACTOR_JOIN_ACCEPTED
                                                ? ZLINK_REQUEST_OK
                                                : ZLINK_REQUEST_REJECTED));
    put_u32 (envelope, join_result_ == ZLINK_ACTOR_JOIN_ACCEPTED ? 0
                                                                 : static_cast<uint32_t> (EACCES));
    put_u32 (envelope, join_result_);
    put_rid (envelope, spot_rid_);
    put_u64 (envelope, spot_generation_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, parts_, part_count_,
                              ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_lookup_reply (mesh_node_t *node_,
                                                const rid_bytes_t &peer_rid_,
                                                uint64_t correlation_,
                                                const zlink_actor_ref_t &ref_,
                                                const rid_bytes_t &spot_rid_,
                                                uint64_t spot_generation_,
                                                uint64_t membership_epoch_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (wire_reply, 0);
    put_u64 (envelope, correlation_);
    put_u32 (envelope, static_cast<uint32_t> (ZLINK_REQUEST_OK));
    put_u32 (envelope, 0);
    put_actor_ref (envelope, ref_);
    put_rid (envelope, spot_rid_);
    put_u64 (envelope, spot_generation_);
    put_u64 (envelope, membership_epoch_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_transfer_ready (mesh_node_t *node_,
                                                  const rid_bytes_t &peer_rid_,
                                                  const zlink_actor_transfer_id_t &transfer_id_,
                                                  const zlink_actor_ref_t &actor_,
                                                  uint64_t expected_epoch_,
                                                  uint64_t final_sequence_,
                                                  uint8_t role_,
                                                  uint64_t offered_messages_,
                                                  uint64_t offered_bytes_,
                                                  const std::vector<transfer_participant_descriptor_t>
                                                    &participants_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (wire_transfer_ready, 0);
    put_transfer_id (envelope, transfer_id_);
    put_actor_ref (envelope, actor_);
    put_u64 (envelope, expected_epoch_);
    put_u64 (envelope, final_sequence_);
    put_u8 (envelope, role_);
    put_u64 (envelope, offered_messages_);
    put_u64 (envelope, offered_bytes_);
    put_u32 (envelope, static_cast<uint32_t> (participants_.size ()));
    for (size_t i = 0; i < participants_.size (); ++i) {
        put_u64 (envelope, participants_[i].participant_id);
        put_u64 (envelope, participants_[i].binding_generation);
        put_u64 (envelope, participants_[i].allowance_messages);
        put_u64 (envelope, participants_[i].allowance_bytes);
    }
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_transfer_data (mesh_node_t *node_,
                                                 const rid_bytes_t &peer_rid_,
                                                 const zlink_actor_transfer_id_t &transfer_id_,
                                                 uint64_t participant_id_,
                                                 uint64_t sequence_,
                                                 const queued_record_t &record_,
                                                 uint64_t relay_serial_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (wire_transfer_data, 0);
    put_transfer_id (envelope, transfer_id_);
    put_u64 (envelope, participant_id_);
    put_u64 (envelope, sequence_);
    put_record_header (envelope, record_, relay_serial_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL,
                              record_.parts.empty () ? NULL : &record_.parts[0],
                              record_.parts.size (), ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_transfer_ack (mesh_node_t *node_,
                                                const rid_bytes_t &peer_rid_,
                                                const zlink_actor_transfer_id_t &transfer_id_,
                                                uint64_t participant_id_,
                                                uint64_t high_water_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (wire_transfer_ack, 0);
    put_transfer_id (envelope, transfer_id_);
    put_u64 (envelope, participant_id_);
    put_u64 (envelope, high_water_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_transfer_seal (
  mesh_node_t *node_,
  const rid_bytes_t &peer_rid_,
  const zlink_actor_transfer_id_t &transfer_id_,
  bool response_,
  const std::vector<transfer_participant_terminal_t> &terminals_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (wire_transfer_seal, 0);
    put_transfer_id (envelope, transfer_id_);
    put_u8 (envelope, response_ ? 1 : 0);
    put_u32 (envelope, static_cast<uint32_t> (terminals_.size ()));
    for (size_t i = 0; i < terminals_.size (); ++i) {
        put_u64 (envelope, terminals_[i].participant_id);
        put_u64 (envelope, terminals_[i].high_water);
    }
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_transfer_complete (
  mesh_node_t *node_,
  const rid_bytes_t &peer_rid_,
  const zlink_actor_transfer_id_t &transfer_id_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (wire_transfer_complete, 0);
    put_transfer_id (envelope, transfer_id_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_reply_relay (mesh_node_t *node_,
                                               const rid_bytes_t &peer_rid_,
                                               uint64_t relay_serial_,
                                               int32_t terminal_result_,
                                               int32_t failure_errno_,
                                               const zlink_msg_t *parts_,
                                               size_t part_count_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (wire_reply_relay, 0);
    put_u64 (envelope, relay_serial_);
    put_u32 (envelope, static_cast<uint32_t> (terminal_result_));
    put_u32 (envelope, static_cast<uint32_t> (failure_errno_));
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, parts_, part_count_,
                              ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_reply (mesh_node_t *node_,
                                         const rid_bytes_t &peer_rid_,
                                         uint64_t correlation_,
                                         int32_t terminal_result_,
                                         int32_t failure_errno_,
                                         const zlink_msg_t *parts_,
                                         size_t part_count_)
{
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    std::vector<unsigned char> envelope = make_envelope (wire_reply, 0);
    put_u64 (envelope, correlation_);
    put_u32 (envelope, static_cast<uint32_t> (terminal_result_));
    put_u32 (envelope, static_cast<uint32_t> (failure_errno_));

    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_data_message (node_, target, envelope, NULL, parts_, part_count_,
                              ZLINK_SEND_FLAGS_NONE);
}
}
}

