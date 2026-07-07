/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/actor/spot/service_spot_actor_gateway_parts_internal.hpp"
#include "api/actor/spot/service_spot_actor_no_bind_internal.hpp"
#include "api/actor/spot/service_spot_actor_state_internal.hpp"
#include "api/message/request_result_internal.hpp"
#include "api/message/submit_result_internal.hpp"
#include "api/service/service_handle_internal.hpp"
#include "api/socket/request_reply_protocol_internal.hpp"
#include "runtime/services/actor/gateway/service_spot_actor_gateway_protocol_internal.hpp"
#include "runtime/services/actor/multipart/service_spot_actor_multipart_internal.hpp"
#include "runtime/services/actor/result/service_spot_actor_result_internal.hpp"
#include "runtime/services/actor/service_spot_actor_internal.hpp"
#include "runtime/services/actor/validation/service_spot_actor_validation_internal.hpp"
#include "runtime/services/spot/node/spot_node.hpp"
#include "utils/routing_id.hpp"

#include <atomic>
#include <string.h>
#include <vector>

namespace
{

uint64_t next_no_bind_request_id ()
{
    static std::atomic<uint64_t> next_id (1);
    uint64_t id = next_id.fetch_add (1);
    if (id == 0)
        id = next_id.fetch_add (1);
    return id;
}

zlink::spot_actor_api_internal::no_bind_pending_key_t
make_no_bind_pending_key (const zlink_routing_id_t &target_node_rid_,
                          const char *actor_id_,
                          uint64_t generation_,
                          const zlink_routing_id_t &caller_endpoint_rid_,
                          uint64_t request_id_)
{
    zlink::spot_actor_api_internal::no_bind_pending_key_t key;
    key.target_node_rid = zlink::routing_id_key (target_node_rid_);
    key.actor_id = actor_id_ ? actor_id_ : "";
    key.generation = generation_;
    key.caller_endpoint_rid = zlink::routing_id_key (caller_endpoint_rid_);
    key.request_id = request_id_;
    return key;
}

void complete_no_bind_callback (zlink_reply_handler_fn handler_,
                                void *userdata_,
                                zlink_request_result_t result_,
                                zlink_msg_t *parts_,
                                size_t part_count_)
{
    if (!handler_)
        return;

    std::vector<zlink_msg_t> moved_parts;
    moved_parts.reserve (part_count_);
    for (size_t i = 0; i < part_count_; ++i) {
        zlink_msg_t moved;
        zlink_msg_init (&moved);
        if (zlink_msg_move (&moved, &parts_[i]) != 0) {
            zlink_msg_close (&moved);
            zlink::request_reply::close_built_parts (&moved_parts);
            zlink::request_reply::complete_reply_callback (handler_, EIO, NULL, 0, userdata_);
            return;
        }
        moved_parts.push_back (moved);
    }

    zlink_msg_t *callback_parts = moved_parts.empty () ? NULL : &moved_parts[0];
    zlink::request_reply::complete_reply_callback (
      handler_,
      zlink::request_result_internal::to_errno (result_),
      callback_parts,
      moved_parts.size (),
      userdata_);
    zlink::request_reply::close_built_parts (&moved_parts);
}

int register_no_bind_pending (
  const zlink::spot_actor_api_internal::no_bind_pending_key_t &key_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  uint32_t timeout_ms_)
{
    return zlink::spot_actor_api_internal::actor_runtime ().no_bind.register_pending (
      key_, handler_, userdata_, timeout_ms_);
}

void erase_no_bind_pending (const zlink::spot_actor_api_internal::no_bind_pending_key_t &key_)
{
    zlink::spot_actor_api_internal::actor_runtime ().no_bind.erase_pending (key_);
}

bool take_no_bind_pending (const zlink::spot_actor_api_internal::no_bind_pending_key_t &key_,
                           zlink::spot_actor_api_internal::no_bind_pending_reply_t *out_)
{
    return zlink::spot_actor_api_internal::actor_runtime ().no_bind.take_pending (key_, out_);
}

zlink_request_result_t request_result_from_actor_lookup_errno (int err_)
{
    return err_ == ESTALE ? ZLINK_REQUEST_CONFLICT : ZLINK_REQUEST_NOT_FOUND;
}

}

namespace zlink
{
namespace spot_actor_api_internal
{

actor_no_bind_reply_t::actor_no_bind_reply_t ()
  : should_send (false),
    result (ZLINK_REQUEST_OK),
    generation (0),
    request_id (0)
{
    memset (&source_node_rid, 0, sizeof (source_node_rid));
    memset (&target_node_rid, 0, sizeof (target_node_rid));
    memset (&caller_endpoint_rid, 0, sizeof (caller_endpoint_rid));
    memset (actor_id, 0, sizeof (actor_id));
}

int process_actor_gateway_no_bind_reply (const zlink_routing_id_t *reply_source_node_rid_,
                                         const zlink::spot_actor_gateway::frame_t &frame_,
                                         zlink_msg_t *payload_parts_,
                                         size_t payload_part_count_)
{
    if (!reply_source_node_rid_
        || !zlink::spot_actor_internal::valid_multipart_payload (payload_parts_,
                                                                 payload_part_count_)) {
        errno = EINVAL;
        return -1;
    }

    const no_bind_pending_key_t key =
      make_no_bind_pending_key (*reply_source_node_rid_, frame_.actor_id, frame_.generation,
                                frame_.session_rid, frame_.request_id);
    no_bind_pending_reply_t pending;
    if (!take_no_bind_pending (key, &pending))
        return 0;

    zlink::request_timeout::cancel (pending.timeout_task);
    const zlink_request_result_t result =
      frame_.join_result_code == 0
        ? ZLINK_REQUEST_OK
        : static_cast<zlink_request_result_t> (frame_.join_result_code);
    complete_no_bind_callback (pending.handler, pending.userdata, result, payload_parts_,
                               payload_part_count_);
    return 0;
}

void prepare_no_bind_reply_after_enqueue (zlink::spot_node_t *node_,
                                          const zlink_routing_id_t *source_node_rid_,
                                          const zlink::spot_actor_gateway::frame_t &frame_,
                                          int enqueue_rc_,
                                          int enqueue_errno_,
                                          actor_no_bind_reply_t *out_)
{
    if (!out_)
        return;
    *out_ = actor_no_bind_reply_t ();
    if (!node_ || !source_node_rid_ || frame_.request_id == 0
        || (enqueue_rc_ == 0 && frame_.join_result_code == 0)) {
        return;
    }

    out_->should_send = true;
    out_->result =
      enqueue_rc_ == 0 ? ZLINK_REQUEST_OK : request_result_from_actor_lookup_errno (enqueue_errno_);
    (void) node_->node_routing_id (&out_->source_node_rid);
    out_->target_node_rid = *source_node_rid_;
    out_->caller_endpoint_rid = frame_.session_rid;
    strncpy (out_->actor_id, frame_.actor_id, ZLINK_ACTOR_ID_MAX - 1);
    out_->generation = frame_.generation;
    out_->request_id = frame_.request_id;
}

zlink_submit_result_t send_no_bind_reply_from_owner (zlink::spot_node_t *owner_node_,
                                                     const zlink_routing_id_t &owner_node_rid_,
                                                     const zlink_routing_id_t &caller_node_rid_,
                                                     const zlink_routing_id_t &caller_endpoint_rid_,
                                                     const char *actor_id_,
                                                     uint64_t generation_,
                                                     uint64_t request_id_,
                                                     zlink_request_result_t result_,
                                                     zlink_msg_t *parts_,
                                                     size_t part_count_)
{
    const int32_t result_code = static_cast<int32_t> (result_);
    if (zlink::spot_actor_internal::same_routing_id (owner_node_rid_, caller_node_rid_)) {
        zlink_msg_t control;
        if (!zlink::spot_actor_gateway::init_control_msg (
              zlink::spot_actor_gateway::packet_actor_to_server_no_bind_reply,
              caller_endpoint_rid_, actor_id_, generation_, ZLINK_PART_FINAL, &control,
              request_id_, result_code)) {
            return zlink::spot_actor_internal::errno_to_submit_result (errno);
        }
        std::vector<zlink_msg_t> gateway_parts;
        if (zlink::spot_actor_internal::build_gateway_parts (&control, parts_, part_count_, false,
                                                             &gateway_parts)
            != 0)
            return zlink::spot_actor_internal::errno_to_submit_result (errno);
        const int rc = zlink::spot_actor_internal::process_gateway_delivery (
          owner_node_, &owner_node_rid_, gateway_parts.data (), gateway_parts.size ());
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (&gateway_parts);
        errno = saved_errno;
        return rc == 0 ? ZLINK_SUBMIT_OK
                       : zlink::spot_actor_internal::errno_to_submit_result (errno);
    }
    return zlink::spot_actor_internal::send_actor_gateway_multipart_from_source (
      owner_node_, owner_node_rid_, caller_node_rid_,
      zlink::spot_actor_gateway::packet_actor_to_server_no_bind_reply, caller_endpoint_rid_,
      actor_id_, generation_, request_id_, result_code, parts_, part_count_, ZLINK_DONTWAIT);
}

zlink_submit_result_t submit_actor_no_bind (void *node_,
                                            const zlink_actor_ref_t *actor_ref_,
                                            zlink_msg_t *parts_,
                                            size_t part_count_,
                                            zlink_reply_handler_fn handler_,
                                            void *userdata_,
                                            zlink_send_flags_t flags_,
                                            uint32_t timeout_ms_,
                                            bool delivery_ack_)
{
    if (!node_) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (!actor_ref_ || !parts_ || part_count_ == 0 || !handler_
        || !zlink::spot_actor_internal::valid_actor_id (actor_ref_->actor_id)
        || !zlink::spot_actor_internal::valid_routing_id (&actor_ref_->node_rid)
        || actor_ref_->generation == 0
        || !zlink::spot_actor_internal::valid_multipart_payload (parts_, part_count_)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }

    zlink::spot_node_t *request_node = static_cast<zlink::spot_node_t *> (node_);
    zlink_routing_id_t source_node_rid;
    memset (&source_node_rid, 0, sizeof (source_node_rid));
    if (request_node->node_routing_id (&source_node_rid) != 0)
        return zlink::spot_actor_internal::errno_to_submit_result (errno);

    const uint64_t request_id = next_no_bind_request_id ();
    const zlink_routing_id_t caller_endpoint_rid = source_node_rid;
    const no_bind_pending_key_t key =
      make_no_bind_pending_key (actor_ref_->node_rid, actor_ref_->actor_id,
                                actor_ref_->generation, caller_endpoint_rid, request_id);
    if (register_no_bind_pending (key, handler_, userdata_, timeout_ms_) != 0)
        return zlink::spot_actor_internal::errno_to_submit_result (errno);

    const int32_t no_bind_flags = delivery_ack_ ? 1 : 0;
    zlink_submit_result_t send_rc = ZLINK_SUBMIT_OK;
    if (zlink::spot_actor_internal::same_routing_id (actor_ref_->node_rid, source_node_rid)) {
        zlink_msg_t control;
        if (!zlink::spot_actor_gateway::init_control_msg (
              zlink::spot_actor_gateway::packet_server_to_actor_no_bind, caller_endpoint_rid,
              actor_ref_->actor_id, actor_ref_->generation, ZLINK_PART_FINAL, &control,
              request_id, no_bind_flags)) {
            erase_no_bind_pending (key);
            return zlink::spot_actor_internal::errno_to_submit_result (errno);
        }

        std::vector<zlink_msg_t> gateway_parts;
        if (zlink::spot_actor_internal::build_gateway_parts (&control, parts_, part_count_, false,
                                                             &gateway_parts)
            != 0) {
            erase_no_bind_pending (key);
            return zlink::spot_actor_internal::errno_to_submit_result (errno);
        }
        if (zlink::spot_actor_internal::process_gateway_delivery (
              request_node, &source_node_rid, gateway_parts.data (), gateway_parts.size ())
            != 0) {
            send_rc = zlink::submit_result_internal::from_errno (errno);
        }
        zlink::request_reply::close_built_parts (&gateway_parts);
    } else {
        send_rc = zlink::spot_actor_internal::send_actor_gateway_multipart_from_source (
          request_node, source_node_rid, actor_ref_->node_rid,
          zlink::spot_actor_gateway::packet_server_to_actor_no_bind, caller_endpoint_rid,
          actor_ref_->actor_id, actor_ref_->generation, request_id, no_bind_flags, parts_,
          part_count_, flags_);
    }
    if (send_rc != ZLINK_SUBMIT_OK) {
        erase_no_bind_pending (key);
        return send_rc;
    }
    return ZLINK_SUBMIT_OK;
}

}
}
