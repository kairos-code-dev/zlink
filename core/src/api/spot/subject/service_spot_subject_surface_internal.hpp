/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_SERVICE_SPOT_SUBJECT_SURFACE_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_SERVICE_SPOT_SUBJECT_SURFACE_INTERNAL_HPP_INCLUDED__

#include "zlink.h"

int spot_dispatch_subscribe_recv_internal (void *spot_,
                                           zlink_routing_id_t *source_rid_out_,
                                           zlink_msg_t **parts_out_,
                                           size_t *part_count_out_,
                                           char *topic_id_out_,
                                           size_t *topic_id_len_out_,
                                           zlink_recv_flags_t flags_);
int spot_dispatch_queue_subscribe_message (void *spot_,
                                           const zlink_routing_id_t *source_rid_,
                                           const char *topic_,
                                           size_t topic_len_,
                                           zlink_msg_t *parts_,
                                           size_t part_count_);
int spot_node_publish_internal (void *node_,
                                const char *topic_id_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                zlink_send_flags_t flags_);
int spot_publish_internal (void *spot_,
                           const char *topic_id_,
                           zlink_msg_t *parts_,
                           size_t part_count_,
                           zlink_send_flags_t flags_);
int spot_pub_publish_internal (void *spot_pub_,
                               const char *topic_id_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               zlink_send_flags_t flags_);
int spot_pub_send_ready_handler_internal (void *spot_pub_,
                                          zlink_send_ready_handler_fn handler_,
                                          void *userdata_);
int spot_sub_subscribe_internal (void *spot_sub_, const char *topic_id_);
int spot_sub_subscribe_pattern_internal (void *spot_sub_, const char *pattern_);
int spot_sub_unsubscribe_internal (void *spot_sub_, const char *topic_id_or_pattern_);
int spot_sub_recv_internal (void *sub_,
                            zlink_routing_id_t *source_rid_out_,
                            zlink_msg_t **parts_,
                            size_t *part_count_,
                            char *topic_id_out_,
                            size_t *topic_id_len_,
                            zlink_recv_flags_t flags_);
int spot_node_recv_internal (void *node_,
                             zlink_routing_id_t *source_rid_out_,
                             zlink_msg_t **parts_,
                             size_t *part_count_,
                             char *topic_id_out_,
                             size_t *topic_id_len_,
                             zlink_recv_flags_t flags_);

#endif
