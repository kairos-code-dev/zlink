/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include <zlink.h>

#include <stddef.h>

namespace zlink
{
class spot_node_t;

namespace spot_actor_gateway
{
struct frame_t;
}

namespace spot_actor_api_internal
{

struct actor_no_bind_reply_t
{
    actor_no_bind_reply_t ();

    bool should_send;
    zlink_request_result_t result;
    zlink_routing_id_t source_node_rid;
    zlink_routing_id_t target_node_rid;
    zlink_routing_id_t caller_endpoint_rid;
    char actor_id[ZLINK_ACTOR_ID_MAX];
    uint64_t generation;
    uint64_t request_id;
};

int process_actor_gateway_no_bind_reply (const zlink_routing_id_t *reply_source_node_rid_,
                                         const zlink::spot_actor_gateway::frame_t &frame_,
                                         zlink_msg_t *payload_parts_,
                                         size_t payload_part_count_);

void prepare_no_bind_reply_after_enqueue (zlink::spot_node_t *node_,
                                          const zlink_routing_id_t *source_node_rid_,
                                          const zlink::spot_actor_gateway::frame_t &frame_,
                                          int enqueue_rc_,
                                          int enqueue_errno_,
                                          actor_no_bind_reply_t *out_);

zlink_submit_result_t send_no_bind_reply_from_owner (zlink::spot_node_t *owner_node_,
                                                     const zlink_routing_id_t &owner_node_rid_,
                                                     const zlink_routing_id_t &caller_node_rid_,
                                                     const zlink_routing_id_t &caller_endpoint_rid_,
                                                     const char *actor_id_,
                                                     uint64_t generation_,
                                                     uint64_t request_id_,
                                                     zlink_request_result_t result_,
                                                     zlink_msg_t *parts_,
                                                     size_t part_count_);

zlink_submit_result_t submit_actor_no_bind (void *node_,
                                            const zlink_actor_ref_t *actor_ref_,
                                            zlink_msg_t *parts_,
                                            size_t part_count_,
                                            zlink_reply_handler_fn handler_,
                                            void *userdata_,
                                            zlink_send_flags_t flags_,
                                            uint32_t timeout_ms_,
                                            bool delivery_ack_);

}
}
