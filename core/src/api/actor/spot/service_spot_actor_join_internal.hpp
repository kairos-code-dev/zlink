/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include <zlink.h>

#include <stddef.h>

namespace zlink
{
namespace spot_actor_api_internal
{

struct actor_handle_t;
struct queued_join_request_t;

bool join_request_live_locked (queued_join_request_t *request_);
void index_join_request_locked (queued_join_request_t *request_);
void unindex_join_request_locked (queued_join_request_t *request_);
void retire_join_request_locked (queued_join_request_t *request_);
void release_join_request_after_completion (queued_join_request_t *request_);
void remove_pending_join_request_locked (queued_join_request_t *request_);
void schedule_join_timeout (queued_join_request_t *request_, uint32_t timeout_ms_);

void remove_join_pending_target_locked (queued_join_request_t *request_);

void complete_join_request (queued_join_request_t *request_, zlink_request_result_t result_);

zlink_submit_result_t complete_immediate_join_result (zlink_msg_t *parts_,
                                                      size_t part_count_,
                                                      zlink_actor_join_spot_handler_fn handler_,
                                                      void *userdata_,
                                                      zlink_request_result_t result_);

zlink_submit_result_t
complete_immediate_entry_join_result (zlink_msg_t *parts_,
                                      size_t part_count_,
                                      zlink_actor_join_entry_spot_handler_fn handler_,
                                      void *userdata_,
                                      const zlink_routing_id_t *target_node_rid_,
                                      zlink_request_result_t result_);

zlink_submit_result_t complete_idempotent_join_async (zlink_msg_t *parts_,
                                                      size_t part_count_,
                                                      zlink_actor_join_spot_handler_fn handler_,
                                                      void *userdata_,
                                                      const zlink_actor_ref_t *actor_,
                                                      const zlink_routing_id_t *spot_rid_,
                                                      uint64_t join_epoch_);

}
}
