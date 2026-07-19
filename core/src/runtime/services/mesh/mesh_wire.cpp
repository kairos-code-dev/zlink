/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <string.h>

#include <memory>
#include <string>
#include <vector>

#include "services/mesh/mesh_wire_internal.hpp"
#include "api/mesh/mesh_c_internal.hpp"
#include "api/monitoring/monitor_api_internal.hpp"
#include "api/socket/part_helper_internal.hpp"
#include "api/socket/socket_api_internal.hpp"
#include "utils/macros.hpp"

//  Transport lifecycle and outbound submits: the node-owned ROUTER, its
//  monitor, endpoint resolution and every wire_submit_* egress path.
namespace zlink
{
namespace mesh
{
//  --- frame transmission -------------------------------------------------------

namespace
{
//  A successful MORE frame opens a part-helper send scope. If any later
//  allocation or frame send fails, roll that scope back before another
//  message uses the node-owned ROUTER.
class wire_send_sequence_guard_t
{
  public:
    explicit wire_send_sequence_guard_t (void *socket_) : _socket (socket_), _open (false) {}
    ~wire_send_sequence_guard_t ()
    {
        if (!_open)
            return;
        const int saved_errno = errno;
        zlink::part_helper_internal::abort_send_step (
          zlink::part_helper_internal::find_handle_state (_socket));
        errno = saved_errno;
    }

    void update (zlink_part_flag_t part_flag_) { _open = part_flag_ == ZLINK_PART_MORE; }

  private:
    void *_socket;
    bool _open;
    wire_send_sequence_guard_t (const wire_send_sequence_guard_t &);
    wire_send_sequence_guard_t &operator= (const wire_send_sequence_guard_t &);
};
}

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
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return -1;
    }
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
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    return send_data_message_unlocked (node_, target_, envelope_, metadata_, parts_, part_count_,
                                       flags_);
}

//  Linearizes a new application wire admission against DRAINING. Taking the
//  node mutex before the wire mutex matches every existing dual-lock path:
//  either the send owns admission before shutdown changes state, or it
//  observes ESHUTDOWN without touching the ROUTER.
zlink_submit_result_t send_application_data_message (
  mesh_node_t *node_,
  const zlink_routing_id_t &target_,
  const std::vector<unsigned char> &envelope_,
  const std::vector<unsigned char> *metadata_,
  const zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_)
{
    std::unique_lock<std::mutex> node_lock (node_->mutex);
    if (node_->state == ZLINK_MESH_NODE_DRAINING
        || node_->state == ZLINK_MESH_NODE_STOPPED) {
        errno = ESHUTDOWN;
        return ZLINK_SUBMIT_INVALID_STATE;
    }
    std::unique_lock<std::mutex> wire_lock (node_->wire_send_mutex);
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }
    node_lock.unlock ();
    return send_data_message_unlocked (node_, target_, envelope_, metadata_, parts_,
                                       part_count_, flags_);
}

zlink_submit_result_t send_data_message_unlocked (mesh_node_t *node_,
                                                  const zlink_routing_id_t &target_,
                                                  const std::vector<unsigned char> &envelope_,
                                                  const std::vector<unsigned char> *metadata_,
                                                  const zlink_msg_t *parts_,
                                                  size_t part_count_,
                                                  zlink_send_flags_t flags_)
{
    wire_send_sequence_guard_t sequence (node_->router_socket);
    const bool has_payload = part_count_ > 0;
    const zlink_part_flag_t envelope_flag =
      (metadata_ || has_payload) ? ZLINK_PART_MORE : ZLINK_PART_FINAL;
    if (send_frame (node_, target_, envelope_, envelope_flag, flags_)
        != 0)
        return submit_errno_result ();
    sequence.update (envelope_flag);
#ifdef ZLINK_BUILD_TESTS
    if (envelope_flag == ZLINK_PART_MORE) {
        try {
            test_maybe_throw_alloc ();
        }
        catch (const std::bad_alloc &) {
            errno = ENOMEM;
            return ZLINK_SUBMIT_OUT_OF_MEMORY;
        }
    }
#endif
    if (metadata_) {
        const zlink_part_flag_t metadata_flag =
          has_payload ? ZLINK_PART_MORE : ZLINK_PART_FINAL;
        if (send_frame (node_, target_, *metadata_, metadata_flag, flags_)
            != 0)
            return submit_errno_result ();
        sequence.update (metadata_flag);
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
        sequence.update (part_flag);
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
    //  Mesh lifecycle allows the same peer RID to reconnect and a higher
    //  generation to supersede its predecessor. Use ROUTER's standard
    //  duplicate-RID handover policy so an asynchronously retiring pipe
    //  cannot reject the successor transport.
    const int duplicate_policy = ZLINK_RID_DUPLICATE_HANDOVER;
    if (zlink_set_option (router, ZLINK_OPT_RID_DUPLICATE_POLICY,
                          &duplicate_policy, sizeof (duplicate_policy))
        != ZLINK_CONFIG_OK) {
        const int saved_errno = errno;
        zlink_close (router);
        errno = saved_errno;
        return -1;
    }
    if (node_->router_hwm_override > 0) {
        const int hwm = node_->router_hwm_override;
        if (zlink_set_option (router, ZLINK_OPT_SNDHWM, &hwm, sizeof (hwm))
              != ZLINK_CONFIG_OK
            || zlink_set_option (router, ZLINK_OPT_RCVHWM, &hwm, sizeof (hwm))
                 != ZLINK_CONFIG_OK) {
            const int saved_errno = errno;
            zlink_close (router);
            errno = saved_errno;
            return -1;
        }
    }
    if (zlink_set_option (router, ZLINK_OPT_SNDTIMEO, &node_->sndtimeo_ms,
                          sizeof (node_->sndtimeo_ms))
        != ZLINK_CONFIG_OK) {
        const int saved_errno = errno;
        zlink_close (router);
        errno = saved_errno;
        return -1;
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
    //  Mesh membership must not lose a physical ready/disconnect edge while
    //  the private inproc PAIR finishes attaching. Version 4 uses the existing
    //  monitor worker retry path; it does not change ROUTER delivery policy.
    node_->router_monitor = open_socket_monitor_internal (
      router, monitor_options.events, 4);

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
    //  A blocking send may use SNDTIMEO=-1 while holding wire_send_mutex.
    //  The socket's stop command is the existing thread-safe cancellation
    //  path: it wakes that send with ETERM so shutdown can acquire the mutex
    //  and close without changing the configured timeout.
    if (node_->router_socket) {
        socket_handle_t handle = as_socket_handle (node_->router_socket);
        if (handle.socket)
            handle.socket->stop ();
    }
    if (node_->io_thread.joinable ())
        node_->io_thread.join ();
    //  All outbound data and control messages use this mutex. Closing under
    //  it lets an already-admitted send finish and makes later senders
    //  observe the null socket instead of racing zlink_close().
    std::lock_guard<std::mutex> lock (node_->wire_send_mutex);
    if (node_->router_monitor)
        zlink_monitor_close (&node_->router_monitor);
    if (node_->router_socket) {
        zlink_close (node_->router_socket);
        node_->router_socket = NULL;
    }
}

int wire_connect_endpoint (mesh_node_t *node_, const std::string &endpoint_)
{
    std::lock_guard<std::mutex> lock (node_->wire_send_mutex);
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return -1;
    }
    if (zlink_connect (node_->router_socket, endpoint_.c_str ()) != ZLINK_CONNECT_OK)
        return -1;
    return 0;
}

int wire_disconnect_endpoint (mesh_node_t *node_, const std::string &endpoint_)
{
    std::lock_guard<std::mutex> lock (node_->wire_send_mutex);
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return -1;
    }
    return zlink_disconnect (node_->router_socket, endpoint_.c_str ())
             == ZLINK_CONNECT_OK
           ? 0
           : -1;
}

int wire_disconnect_peer (mesh_node_t *node_, const rid_bytes_t &peer_rid_)
{
    std::lock_guard<std::mutex> lock (node_->wire_send_mutex);
    if (!node_->router_socket) {
        errno = ENOTCONN;
        return -1;
    }
    const zlink_routing_id_t rid = rid_value (peer_rid_);
    return zlink_disconnect_rid (node_->router_socket, &rid)
             == ZLINK_CONNECT_OK
           ? 0
           : -1;
}

void wire_broadcast_update (mesh_node_t *node_)
{
    std::vector<unsigned char> frame = make_envelope (wire_update, 0);
    std::vector<rid_bytes_t> targets;
    {
        std::lock_guard<std::mutex> lock (node_->mutex);
        encode_descriptor_locked (node_, frame);
        targets.reserve (node_->peers.size ());
        for (size_t i = 0; i < node_->peers.size (); ++i) {
            if (node_->peers[i].state == ZLINK_MESH_PEER_ADMITTED)
                targets.push_back (node_->peers[i].rid);
        }
    }
    for (size_t i = 0; i < targets.size (); ++i)
        send_control (node_, rid_value (targets[i]), frame);
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
    return send_application_data_message (
      node_, target, envelope, metadata_ ? &metadata : NULL, parts_, part_count_, flags_);
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
    return send_application_data_message (
      node_, target, envelope, metadata_ ? &metadata : NULL, parts_, part_count_, flags_);
}

zlink_submit_result_t wire_publish_remote (mesh_node_t *node_,
                                           const std::vector<rid_bytes_t> &targets_,
                                           const std::string &channel_,
                                           const std::string &topic_,
                                           const rid_bytes_t &source_spot_rid_,
                                           const zlink_mesh_metadata_view_t *metadata_,
                                           const zlink_msg_t *parts_,
                                           size_t part_count_,
                                           zlink_send_flags_t flags_,
                                           uint32_t *admitted_out_,
                                           uint32_t *dropped_out_,
                                           uint32_t *unreachable_out_)
{
    *admitted_out_ = 0;
    *dropped_out_ = 0;
    *unreachable_out_ = 0;
    if (targets_.empty ())
        return ZLINK_SUBMIT_OK;

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

    //  Each target takes the normal per-message ROUTER send lock. Releasing
    //  it between targets prevents one fan-out from reserving the whole
    //  outbound path while preserving multipart framing per target.
    zlink_submit_result_t aggregate_rc = ZLINK_SUBMIT_OK;
    int aggregate_errno = 0;
    for (size_t i = 0; i < targets_.size (); ++i) {
        const zlink_routing_id_t target = rid_value (targets_[i]);
        const zlink_submit_result_t rc =
          send_application_data_message (
            node_, target, envelope, metadata_ ? &metadata : NULL, parts_, part_count_, flags_);
        if (rc == ZLINK_SUBMIT_OK)
            *admitted_out_ += 1;
        else if (rc == ZLINK_SUBMIT_NOT_CONNECTED)
            *unreachable_out_ += 1;
        else {
            *dropped_out_ += 1;
            if (rc == ZLINK_SUBMIT_BACKPRESSURED) {
                if (aggregate_rc == ZLINK_SUBMIT_OK) {
                    aggregate_rc = rc;
                    aggregate_errno = errno;
                }
            } else {
                //  A hard failure is more actionable than capacity pressure
                //  observed at an earlier target.
                aggregate_rc = rc;
                aggregate_errno = errno;
                *dropped_out_ += static_cast<uint32_t> (targets_.size () - i - 1);
                break;
            }
        }
    }
    if (aggregate_rc != ZLINK_SUBMIT_OK)
        errno = aggregate_errno;
    return aggregate_rc;
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
    return send_application_data_message (
      node_, target, envelope, metadata_ ? &metadata : NULL, parts_, part_count_, flags_);
}

zlink_submit_result_t wire_submit_actor_lookup (mesh_node_t *node_,
                                                const rid_bytes_t &peer_rid_,
                                                uint64_t correlation_,
                                                const std::string &actor_id_)
{
    std::vector<unsigned char> envelope = make_envelope (wire_actor_lookup, 0);
    put_u64 (envelope, correlation_);
    put_u8 (envelope, static_cast<unsigned char> (actor_id_.size ()));
    put_bytes (envelope, actor_id_.data (), actor_id_.size ());
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_application_data_message (
      node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
}

zlink_submit_result_t wire_submit_actor_destroy (mesh_node_t *node_,
                                                 const rid_bytes_t &peer_rid_,
                                                 uint64_t correlation_,
                                                 const zlink_actor_ref_t &actor_)
{
    std::vector<unsigned char> envelope = make_envelope (wire_actor_destroy, 0);
    put_u64 (envelope, correlation_);
    put_actor_ref (envelope, actor_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_application_data_message (
      node_, target, envelope, NULL, NULL, 0, ZLINK_SEND_FLAGS_NONE);
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
    std::vector<unsigned char> envelope = make_envelope (wire_actor_join, 0);
    put_u64 (envelope, correlation_);
    put_actor_ref (envelope, actor_);
    put_u8 (envelope, entry_ ? 1 : 0);
    put_rid (envelope, target_spot_rid_);
    put_u64 (envelope, target_spot_generation_);
    const zlink_routing_id_t target = rid_value (peer_rid_);
    return send_application_data_message (
      node_, target, envelope, NULL, creation_parts_, creation_part_count_,
      ZLINK_SEND_FLAGS_NONE);
}

void wire_notify_actor_left (mesh_node_t *node_,
                             const rid_bytes_t &peer_rid_,
                             const zlink_actor_ref_t &actor_,
                             const rid_bytes_t &previous_spot_rid_,
                             uint64_t previous_spot_generation_,
                             uint64_t previous_membership_epoch_,
                             uint64_t current_membership_epoch_)
{
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
                                              size_t part_count_,
                                              zlink_send_flags_t flags_)
{
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
    //  The public join-reply contract owns the flags: DONTWAIT surfaces
    //  EAGAIN and a blocking call waits for SNDTIMEO before ETIMEDOUT, both
    //  leaving the reply token retryable (04-actor §3).
    return send_data_message (node_, target, envelope, NULL, parts_, part_count_, flags_);
}

zlink_submit_result_t wire_submit_lookup_reply (mesh_node_t *node_,
                                                const rid_bytes_t &peer_rid_,
                                                uint64_t correlation_,
                                                const zlink_actor_ref_t &ref_,
                                                const rid_bytes_t &spot_rid_,
                                                uint64_t spot_generation_,
                                                uint64_t membership_epoch_)
{
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
