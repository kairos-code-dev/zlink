/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_SERVICE_OPTION_SURFACE_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_SERVICE_OPTION_SURFACE_INTERNAL_HPP_INCLUDED__

#include "zlink.h"

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
int zlink_service_spot_node_refresh_external_router_identity (void *node_handle_);
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

#endif
