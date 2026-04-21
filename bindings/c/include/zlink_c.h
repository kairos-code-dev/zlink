/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_C_H
#define ZLINK_C_H

#include <zlink.h>

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(ZLINK_EXPORT)
#define ZLINK_C_EXPORT ZLINK_EXPORT
#elif !defined(_WIN32) && !defined(ZLINK_STATIC)
#define ZLINK_C_EXPORT __attribute__((visibility("default")))
#else
#define ZLINK_C_EXPORT
#endif

ZLINK_C_EXPORT zlink_submit_result_t zlink_send (void *s_,
                                                 zlink_msg_t *parts_,
                                                 size_t part_count_,
                                                 zlink_send_flags_t flags_);

ZLINK_C_EXPORT zlink_submit_result_t zlink_send_rid (
  void *s_,
  const zlink_routing_id_t *target_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_);

ZLINK_C_EXPORT zlink_submit_result_t zlink_publish (void *subject_,
                                                    const char *topic_id_,
                                                    zlink_msg_t *parts_,
                                                    size_t part_count_,
                                                    zlink_send_flags_t flags_);

ZLINK_C_EXPORT zlink_submit_result_t zlink_dealer_request (
  void *dealer_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);

ZLINK_C_EXPORT zlink_submit_result_t zlink_router_request (
  void *router_,
  const zlink_routing_id_t *peer_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);

ZLINK_C_EXPORT zlink_submit_result_t zlink_router_reply (
  void *router_,
  const zlink_routing_id_t *peer_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_);

ZLINK_C_EXPORT zlink_submit_result_t zlink_router_request_spot (
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);

ZLINK_C_EXPORT zlink_submit_result_t zlink_router_reply_spot (
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_);

ZLINK_C_EXPORT zlink_submit_result_t zlink_router_send_spot (
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_);

/* Caller must free(parts_out) after use; zlink_multipart_close closes messages only. */
ZLINK_C_EXPORT zlink_recv_result_t zlink_recv (void *s_,
                                               zlink_routing_id_t *source_rid_out_,
                                               zlink_msg_t **parts_out_,
                                               size_t *part_count_out_,
                                               zlink_recv_flags_t flags_);

/* Caller must free(parts_out) after use; zlink_multipart_close closes messages only. */
ZLINK_C_EXPORT zlink_recv_result_t zlink_subscribe (
  void *subject_,
  zlink_routing_id_t *source_rid_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_);

/* Caller must free(parts_out) after use; zlink_multipart_close closes messages only. */
ZLINK_C_EXPORT zlink_recv_result_t zlink_router_recv (
  void *router_,
  const zlink_routing_id_t **source_node_rid_out_,
  const zlink_routing_id_t **source_spot_rid_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  zlink_recv_flags_t flags_);

ZLINK_C_EXPORT zlink_recv_result_t zlink_subscription_event (
  void *subject_,
  zlink_routing_id_t *source_rid_out_,
  int *subscribed_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_);

ZLINK_C_EXPORT zlink_submit_result_t zlink_spot_send_channel (
  void *spot_,
  const char *channel_name_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_);

ZLINK_C_EXPORT zlink_submit_result_t zlink_spot_publish (
  void *spot_,
  const char *service_name_,
  const char *topic_id_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_);

/* Caller must free(parts_out) after use; zlink_multipart_close closes messages only. */
ZLINK_C_EXPORT zlink_recv_result_t zlink_spot_subscribe (
  void *spot_,
  zlink_routing_id_t *source_rid_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  char *service_name_out_,
  size_t *service_name_len_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_);

ZLINK_C_EXPORT zlink_recv_result_t zlink_spot_subscription_event (
  void *spot_,
  zlink_routing_id_t *source_rid_out_,
  int *subscribed_out_,
  char *service_name_out_,
  size_t *service_name_len_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_);

ZLINK_C_EXPORT zlink_submit_result_t zlink_spot_request_channel (
  void *spot_,
  const char *channel_name_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);

ZLINK_C_EXPORT zlink_submit_result_t zlink_spot_request_spot (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);

ZLINK_C_EXPORT zlink_submit_result_t zlink_spot_request_router (
  void *spot_,
  const zlink_routing_id_t *peer_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);

ZLINK_C_EXPORT zlink_submit_result_t zlink_spot_reply_spot (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_);

ZLINK_C_EXPORT zlink_submit_result_t zlink_spot_reply_router (
  void *spot_,
  const zlink_routing_id_t *peer_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_);

/* Caller must free(parts_out) after use; zlink_multipart_close closes messages only. */
ZLINK_C_EXPORT zlink_recv_result_t zlink_spot_recv (
  void *spot_,
  const zlink_routing_id_t **source_rid_out_,
  const zlink_routing_id_t **spot_rid_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  zlink_recv_flags_t flags_);

#if defined(__cplusplus)
}
#endif

#endif
