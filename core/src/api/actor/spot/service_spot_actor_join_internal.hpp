/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include <zlink.h>

#include <stddef.h>
#include <deque>
#include <memory>
#include <vector>

struct spot_handle_t;
struct spot_logical_state_t;

namespace zlink
{
class spot_node_t;

namespace spot_actor_gateway
{
struct frame_t;
}

namespace spot_actor_api_internal
{

struct actor_handle_t;
struct queued_join_request_t;

struct join_request_completion_batch_t
{
    std::deque<queued_join_request_t *> requests;
};

typedef void (*join_actor_session_clear_fn) (actor_handle_t *actor_, void *userdata_);

uint64_t next_join_commit_epoch_locked ();
zlink_routing_id_t join_actor_current_spot_rid_locked (const actor_handle_t *actor_);
bool join_actor_has_pending_request_locked (const actor_handle_t *actor_);
bool join_actor_in_entry_spot_locked (const actor_handle_t *actor_);
bool join_actor_in_user_spot_locked (const actor_handle_t *actor_);
size_t join_pending_count_for_spot_locked (spot_logical_state_t *spot_state_);

actor_handle_t *create_join_actor_locked_with_generation (zlink::spot_node_t *node_,
                                                          const zlink_routing_id_t &node_rid_,
                                                          const char *actor_id_,
                                                          uint64_t generation_,
                                                          bool pending_remote_join_);
void remove_join_pending_target_locked (queued_join_request_t *request_);
void remove_join_actor_locked (actor_handle_t *actor_, bool erase_session_binding_);
void notify_join_actor_readable (actor_handle_t *actor_);
void schedule_join_lifecycle_event_locked (
  const std::shared_ptr<spot_logical_state_t> &spot_state_,
  zlink_spot_actor_lifecycle_event_kind_t kind_,
  const zlink_spot_actor_lifecycle_info_t &info_);
zlink_spot_actor_lifecycle_info_t make_join_lifecycle_info (
  const zlink_actor_ref_t &previous_actor_,
  const zlink_actor_ref_t &current_actor_,
  const zlink_routing_id_t &previous_spot_rid_,
  const zlink_routing_id_t &current_spot_rid_,
  uint64_t join_epoch_);

int process_actor_gateway_join_packet_locked (
  zlink::spot_node_t *node_,
  const zlink_routing_id_t *source_node_rid_,
  const zlink::spot_actor_gateway::frame_t &frame_,
  zlink_msg_t *parts_,
  size_t part_count_,
  bool *handled_out_,
  spot_handle_t **notify_spot_out_,
  join_request_completion_batch_t *completed_out_,
  actor_handle_t **source_actor_to_remove_out_);

void abort_join_requests_for_stream_locked (void *stream_,
                                            join_actor_session_clear_fn clear_session_,
                                            void *userdata_,
                                            join_request_completion_batch_t *aborted_);

void complete_and_release_join_requests (join_request_completion_batch_t *requests_,
                                         zlink_request_result_t result_);
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
