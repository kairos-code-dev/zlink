/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_SERVICE_SURFACE_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_SERVICE_SURFACE_INTERNAL_HPP_INCLUDED__

#include "zlink.h"

#include "api/poller_api_internal.hpp"

int spot_dispatch_subscribe_recv_internal (void *spot_,
                                          zlink_routing_id_t *source_rid_out_,
                                          zlink_msg_t **parts_out_,
                                          size_t *part_count_out_,
                                          char *topic_id_out_,
                                          size_t *topic_id_len_out_,
                                          zlink_recv_flags_t flags_);
int spot_dispatch_queue_subscribe_message (
  void *spot_,
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
int spot_sub_subscribe_pattern_internal (void *spot_sub_,
                                         const char *pattern_);
int spot_sub_unsubscribe_internal (void *spot_sub_,
                                   const char *topic_id_or_pattern_);
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
extern "C" void zlink_spot_notify_dispatch_event (
  void *spot_,
  zlink_spot_dispatch_event_t event_);
extern "C" void zlink_spot_notify_dispatch_info (
  void *spot_,
  zlink_spot_dispatch_event_t event_,
  zlink_spot_dispatch_subject_kind_t subject_kind_,
  void *subject_);
extern "C" int zlink_spot_install_peer_route_dispatch (void *node_,
                                                        void *socket_);
extern "C" int zlink_spot_process_route_ingress (void *node_, void *socket_);
extern "C" int zlink_spot_process_peer_route_ingress (void *node_, void *socket_);
extern "C" int zlink_spot_process_node_router (void *node_, void *socket_);

int zlink_spot_subject_set_common_option_internal (void *handle_,
                                                   zlink_option_t option_,
                                                   const void *optval_,
                                                   size_t optvallen_);
int zlink_spot_subject_get_common_option_internal (void *handle_,
                                                   zlink_option_t option_,
                                                   void *optval_,
                                                   size_t *optvallen_);
int zlink_spot_subject_set_pub_option_internal (void *handle_,
                                                zlink_pub_option_t option_,
                                                const void *optval_,
                                                size_t optvallen_);
int zlink_spot_subject_get_pub_option_internal (void *handle_,
                                                zlink_pub_option_t option_,
                                                void *optval_,
                                                size_t *optvallen_);
int zlink_spot_subject_set_sub_option_internal (void *handle_,
                                                zlink_sub_option_t option_,
                                                const void *optval_,
                                                size_t optvallen_);
int zlink_spot_subject_get_sub_option_internal (void *handle_,
                                                zlink_sub_option_t option_,
                                                void *optval_,
                                                size_t *optvallen_);
int zlink_spot_subject_set_routing_id_internal (void *handle_,
                                                const void *data_,
                                                size_t size_);
int zlink_spot_subject_get_routing_id_internal (void *handle_,
                                                zlink_routing_id_t *out_);
int zlink_spot_subject_set_weight_internal (
  void *handle_,
  uint32_t state_);
int zlink_spot_subject_get_weight_internal (
  void *handle_,
  uint32_t *state_out_);
int zlink_spot_subject_set_tls_server_internal (void *handle_,
                                                const char *cert_,
                                                const char *key_,
                                                int require_client_cert_);
int zlink_spot_subject_set_tls_client_internal (void *handle_,
                                                const char *ca_cert_,
                                                const char *hostname_,
                                                int trust_system_);
int zlink_spot_subject_set_subscription_internal (void *handle_,
                                                  const char *filter_);
int zlink_spot_subject_unset_subscription_internal (void *handle_,
                                                    const char *filter_);
int zlink_spot_subject_subscription_at_internal (void *handle_,
                                                 size_t index_,
                                                 char *filter_out_,
                                                 size_t *filter_len_inout_,
                                                 int *is_pattern_out_);
int zlink_service_set_common_option (void *handle_,
                                     zlink_option_t option_,
                                     int socket_option_,
                                     const void *optval_,
                                     size_t optvallen_);
int zlink_service_get_common_option (void *handle_,
                                     zlink_option_t option_,
                                     int socket_option_,
                                     void *optval_,
                                     size_t *optvallen_);
int zlink_service_set_routing_id (void *handle_,
                                  const void *data_,
                                  size_t size_);
int zlink_service_get_routing_id (void *handle_, zlink_routing_id_t *out_);
int zlink_service_set_weight (void *handle_,
                                       uint32_t state_);
int zlink_service_get_weight (void *handle_,
                                       uint32_t *state_out_);
int zlink_service_set_tls_server (void *handle_,
                                  const char *cert_,
                                  const char *key_,
                                  int require_client_cert_);
int zlink_service_set_tls_client (void *handle_,
                                  const char *ca_cert_,
                                  const char *hostname_,
                                  int trust_system_);
int zlink_service_set_router_option (void *handle_,
                                     zlink_router_option_t option_,
                                     int socket_option_,
                                     const void *optval_,
                                     size_t optvallen_);
int zlink_service_get_router_option (void *handle_,
                                     zlink_router_option_t option_,
                                     int socket_option_,
                                     void *optval_,
                                     size_t *optvallen_);
int zlink_service_set_pub_option (void *handle_,
                                  zlink_pub_option_t option_,
                                  int socket_option_,
                                  const void *optval_,
                                  size_t optvallen_);
int zlink_service_get_pub_option (void *handle_,
                                  zlink_pub_option_t option_,
                                  int socket_option_,
                                  void *optval_,
                                  size_t *optvallen_);
int zlink_service_set_sub_option (void *handle_,
                                  zlink_sub_option_t option_,
                                  int socket_option_,
                                  const void *optval_,
                                  size_t optvallen_);
int zlink_service_get_sub_option (void *handle_,
                                  zlink_sub_option_t option_,
                                  int socket_option_,
                                  void *optval_,
                                  size_t *optvallen_);
int zlink_service_set_subscription (void *handle_, const char *filter_);
int zlink_service_unset_subscription (void *handle_, const char *filter_);
int zlink_service_subscription_at (void *handle_,
                                   size_t index_,
                                   char *filter_out_,
                                   size_t *filter_len_inout_,
                                   int *is_pattern_out_);
int zlink_service_spot_set_common_option_internal (void *handle_,
                                                   zlink_option_t option_,
                                                   int socket_option_,
                                                   const void *optval_,
                                                   size_t optvallen_);
int zlink_service_spot_get_common_option_internal (void *handle_,
                                                   zlink_option_t option_,
                                                   int socket_option_,
                                                   void *optval_,
                                                   size_t *optvallen_);
int zlink_service_spot_set_routing_id_internal (void *handle_,
                                                const void *data_,
                                                size_t size_);
int zlink_service_spot_get_routing_id_internal (void *handle_,
                                                zlink_routing_id_t *out_);
int zlink_service_spot_node_refresh_routed_mesh_subscription (void *node_handle_);
int zlink_service_spot_set_weight_internal (
  void *handle_,
  uint32_t state_);
int zlink_service_spot_get_weight_internal (
  void *handle_,
  uint32_t *state_out_);
int zlink_service_spot_set_tls_server_internal (
  void *handle_,
  const char *cert_,
  const char *key_,
  int require_client_cert_);
int zlink_service_spot_set_tls_client_internal (void *handle_,
                                                const char *ca_cert_,
                                                const char *hostname_,
                                                int trust_system_);
int zlink_service_spot_set_pub_option_internal (void *handle_,
                                                zlink_pub_option_t option_,
                                                int socket_option_,
                                                const void *optval_,
                                                size_t optvallen_);
int zlink_service_spot_get_pub_option_internal (void *handle_,
                                                zlink_pub_option_t option_,
                                                int socket_option_,
                                                void *optval_,
                                                size_t *optvallen_);
int zlink_service_spot_set_sub_option_internal (void *handle_,
                                                zlink_sub_option_t option_,
                                                int socket_option_,
                                                const void *optval_,
                                                size_t optvallen_);
int zlink_service_spot_get_sub_option_internal (void *handle_,
                                                zlink_sub_option_t option_,
                                                int socket_option_,
                                                void *optval_,
                                                size_t *optvallen_);
int zlink_service_spot_set_subscription_internal (void *handle_,
                                                  const char *filter_);
int zlink_service_spot_unset_subscription_internal (void *handle_,
                                                    const char *filter_);
int zlink_service_spot_subscription_at_internal (void *handle_,
                                                 size_t index_,
                                                 char *filter_out_,
                                                 size_t *filter_len_inout_,
                                                 int *is_pattern_out_);
int zlink_service_send_ready_handler_internal (
  void *handle_,
  zlink_send_ready_handler_fn handler_,
  void *userdata_);
int zlink_service_poller_add_internal (poller_handle_t *poller_,
                                       void *socket_,
                                       void *user_data_,
                                       short events_);
int zlink_service_poller_modify_internal (poller_handle_t *poller_,
                                          void *socket_,
                                          short events_);
int zlink_service_poller_remove_internal (poller_handle_t *poller_,
                                          void *socket_);
int zlink_service_send_internal (void *handle_,
                                 zlink_msg_t *parts_,
                                 size_t part_count_,
                                 zlink_send_flags_t flags_);
int zlink_service_send_rid_internal (void *handle_,
                                     const zlink_routing_id_t *target_rid_,
                                     zlink_msg_t *parts_,
                                     size_t part_count_,
                                     zlink_send_flags_t flags_);
extern "C" int zlink_service_publish_internal (void *subject_,
                                               const char *topic_id_,
                                               zlink_msg_t *parts_,
                                               size_t part_count_,
                                               zlink_send_flags_t flags_);
int zlink_service_recv_internal (void *handle_,
                                 zlink_routing_id_t *source_rid_out_,
                                 zlink_msg_t **parts_out_,
                                 size_t *part_count_out_,
                                 zlink_recv_flags_t flags_);
extern "C" int zlink_service_subscribe_recv_internal (
  void *subject_,
  zlink_routing_id_t *source_rid_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_);

#endif
