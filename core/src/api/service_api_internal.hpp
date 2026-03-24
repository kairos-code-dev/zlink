/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_SERVICE_API_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_SERVICE_API_INTERNAL_HPP_INCLUDED__

#include "zlink.h"

#include "api/poller_api_internal.hpp"
#include "services/common/service_public_api.hpp"
#include "services/spot/spot_handle.hpp"
#include "services/spot/spot_subject_access.hpp"

namespace zlink
{
class gateway_t;
}

bool is_registered_gateway_handle (void *gateway_);
bool is_registered_spot_node_handle (void *node_);
bool is_registered_spot_handle (void *spot_);

void register_gateway_mode_state (zlink::gateway_t *gateway_);
void register_spot_node_mode_state (zlink::spot_node_t *node_);
void register_spot_mode_state (spot_handle_t *spot_);
void erase_gateway_mode_state (zlink::gateway_t *gateway_);
void erase_spot_node_mode_state (zlink::spot_node_t *node_);
void erase_spot_mode_state (spot_handle_t *spot_);
int gateway_transition_to_callback_mode (zlink::gateway_t *gateway_);
void gateway_revert_callback_transition (zlink::gateway_t *gateway_);
int spot_node_transition_to_callback_mode (zlink::spot_node_t *node_);
void spot_node_revert_callback_transition (zlink::spot_node_t *node_);
int spot_transition_to_callback_mode (spot_handle_t *spot_);
void spot_revert_callback_transition (spot_handle_t *spot_);
int gateway_require_recv_model (zlink::gateway_t *gateway_);
int spot_node_require_recv_model (zlink::spot_node_t *node_);
int spot_require_recv_model (spot_handle_t *spot_);
int gateway_activate_send_ready_mode (zlink::gateway_t *gateway_,
                                      bool *already_active_out_);
void gateway_revert_send_ready_mode (zlink::gateway_t *gateway_);
int spot_activate_send_ready_mode (spot_handle_t *spot_,
                                   bool *already_active_out_);
void spot_revert_send_ready_mode (spot_handle_t *spot_);
int spot_node_activate_send_ready_mode (zlink::spot_node_t *node_,
                                        bool *already_active_out_);
void spot_node_revert_send_ready_mode (zlink::spot_node_t *node_);
int increment_gateway_poller_ref (zlink::gateway_t *gateway_);
int increment_gateway_poller_ref (zlink::gateway_t *gateway_, short events_);
void decrement_gateway_poller_ref (zlink::gateway_t *gateway_);
void decrement_gateway_poller_ref (zlink::gateway_t *gateway_, short events_);
int increment_spot_node_poller_ref (zlink::spot_node_t *node_);
int increment_spot_node_poller_ref (zlink::spot_node_t *node_, short events_);
void decrement_spot_node_poller_ref (zlink::spot_node_t *node_);
void decrement_spot_node_poller_ref (zlink::spot_node_t *node_, short events_);
int increment_spot_poller_ref (spot_handle_t *spot_);
int increment_spot_poller_ref (spot_handle_t *spot_, short events_);
void decrement_spot_poller_ref (spot_handle_t *spot_);
void decrement_spot_poller_ref (spot_handle_t *spot_, short events_);
int validate_recv_flags (int flags_);
int validate_spot_generic_poller_events (short events_, bool *is_pub_out_);
void release_poller_registration (const poller_registration_t &registration_);
int poller_add_gateway_registration (poller_handle_t *poller_,
                                     void *gateway_,
                                     void *user_data_,
                                     short events_);
int poller_modify_gateway_registration (poller_handle_t *poller_,
                                        void *gateway_,
                                        short events_);
int increment_spot_subject_poller_ref (void *spot_or_node_, short events_);
poller_subject_kind_t poller_spot_pub_kind_for_subject (void *spot_or_node_);
poller_subject_kind_t poller_spot_sub_kind_for_subject (void *spot_or_node_);

int gateway_send_parts (void *gateway_,
                        zlink_msg_t *parts_,
                        size_t part_count_,
                        zlink_send_flags_t flags_);
int gateway_send_parts_rid (void *gateway_,
                            const zlink_routing_id_t *routing_id_,
                            zlink_msg_t *parts_,
                            size_t part_count_,
                            zlink_send_flags_t flags_);
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
                            zlink_send_flags_t flags_);
int spot_node_recv_internal (void *node_,
                             zlink_routing_id_t *source_rid_out_,
                             zlink_msg_t **parts_,
                             size_t *part_count_,
                             char *topic_id_out_,
                             size_t *topic_id_len_,
                             zlink_send_flags_t flags_);

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
int zlink_service_recv_handler_internal (void *handle_,
                                         zlink_subscribe_handler_fn handler_,
                                         void *userdata_);
int zlink_service_msg_recv_handler_internal (
  void *handle_,
  zlink_socket_msg_handler_fn handler_,
  void *userdata_);
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
int zlink_service_publish_internal (void *subject_,
                                    const char *topic_id_,
                                    zlink_msg_t *parts_,
                                    size_t part_count_,
                                    zlink_send_flags_t flags_);
int zlink_service_recv_internal (void *handle_,
                                 zlink_routing_id_t *source_rid_out_,
                                 zlink_msg_t **parts_out_,
                                 size_t *part_count_out_,
                                 zlink_send_flags_t flags_);
int zlink_service_subscribe_recv_internal (void *subject_,
                                           zlink_routing_id_t *source_rid_out_,
                                           zlink_msg_t **parts_out_,
                                           size_t *part_count_out_,
                                           char *topic_id_out_,
                                           size_t *topic_id_len_out_,
                                           zlink_send_flags_t flags_);
#endif
