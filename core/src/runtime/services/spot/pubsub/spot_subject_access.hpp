/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_SUBJECT_ACCESS_HPP_INCLUDED__
#define __ZLINK_SPOT_SUBJECT_ACCESS_HPP_INCLUDED__

#include "services/spot/runtime/spot_handle.hpp"

namespace zlink
{
class socket_base_t;
class spot_node_t;
class spot_pub_t;
class spot_sub_t;
}

zlink::spot_pub_t *as_spot_pub_side_handle (void *handle_);
zlink::spot_sub_t *as_spot_sub_side_handle (void *handle_);
zlink::spot_node_t *as_spot_node_handle (void *handle_);
spot_handle_t *as_spot_handle (void *spot_);

zlink::socket_base_t *spot_pub_poller_socket (void *spot_pub_);
zlink::socket_base_t *spot_sub_poller_socket (void *spot_sub_);
zlink::socket_base_t *resolve_spot_pub_subject_poller_socket (void *spot_or_node_);
int resolve_spot_pub_subject_poller_fd (void *spot_or_node_, zlink_fd_t *fd_out_);
zlink::socket_base_t *resolve_spot_sub_subject_poller_socket (void *spot_or_node_);
int resolve_spot_sub_subject_poller_fd (void *spot_or_node_, zlink_fd_t *fd_out_);
void drain_spot_send_ready_signal (void *spot_or_node_);
void notify_spot_send_ready_recovery (zlink::spot_node_t *node_);
int resolve_spot_send_timeout_ms (void *spot_or_node_);

static inline void *resolve_spot_sub_side_handle (void *handle_)
{
    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (handle_))
        return sub;

    spot_handle_t *spot = as_spot_handle (handle_);
    if (spot) {
        errno = ENOTSUP;
        return NULL;
    }
    return NULL;
}

int infer_spot_monitor_role (void *target_, uint32_t events_);
int spot_subject_publish (void *subject_,
                          const char *topic_id_,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          zlink_send_flags_t flags_);
int spot_subject_recv (void *subject_,
                       zlink_routing_id_t *source_rid_out_,
                       zlink_msg_t **parts_out_,
                       size_t *part_count_out_,
                       char *topic_id_out_,
                       size_t *topic_id_len_out_,
                       zlink_recv_flags_t flags_);
int spot_subject_set_common_option (void *handle_,
                                    zlink_option_t option_,
                                    const void *optval_,
                                    size_t optvallen_);
int spot_subject_get_common_option (void *handle_,
                                    zlink_option_t option_,
                                    void *optval_,
                                    size_t *optvallen_);
int spot_subject_set_pub_option (void *handle_,
                                 zlink_pub_option_t option_,
                                 const void *optval_,
                                 size_t optvallen_);
int spot_subject_get_pub_option (void *handle_,
                                 zlink_pub_option_t option_,
                                 void *optval_,
                                 size_t *optvallen_);
int spot_subject_set_sub_option (void *handle_,
                                 zlink_sub_option_t option_,
                                 const void *optval_,
                                 size_t optvallen_);
int spot_subject_get_sub_option (void *handle_,
                                 zlink_sub_option_t option_,
                                 void *optval_,
                                 size_t *optvallen_);
int spot_subject_set_routing_id (void *handle_, const void *data_, size_t size_);
int spot_subject_get_routing_id (void *handle_, zlink_routing_id_t *out_);
int spot_subject_set_tls_server (void *handle_,
                                 const char *cert_,
                                 const char *key_,
                                 int require_client_cert_);
int spot_subject_set_tls_client (void *handle_,
                                 const char *ca_cert_,
                                 const char *hostname_,
                                 int trust_system_);
int spot_subject_set_subscription (void *handle_, const char *filter_);
int spot_subject_unset_subscription (void *handle_, const char *filter_);
int recv_logical_spot_subscription (spot_handle_t *spot_,
                                    zlink_routing_id_t *source_rid_out_,
                                    zlink_msg_t **parts_out_,
                                    size_t *part_count_out_,
                                    char *topic_id_out_,
                                    size_t *topic_id_len_out_);
int spot_subject_subscription_at (
  void *handle_, size_t index_, char *filter_out_, size_t *filter_len_inout_, int *is_pattern_out_);
int spot_pub_install_send_ready_handler (void *spot_pub_,
                                         zlink_send_ready_handler_fn handler_,
                                         void *userdata_);

bool in_spot_node_send_ready_callback (zlink::spot_node_t *node_);
zlink::spot_node_t *enter_spot_node_send_ready_callback (zlink::spot_node_t *node_);
void leave_spot_node_send_ready_callback (zlink::spot_node_t *previous_);
void clear_spot_node_handler_registration (zlink::spot_node_t *node_);
void spot_subject_composite_sub_handler_adapter (const zlink_routing_id_t *source_rid_,
                                                 const char *topic_,
                                                 size_t topic_len_,
                                                 zlink_msg_t *parts_,
                                                 size_t part_count_,
                                                 void *userdata_);
int spot_install_handler (spot_handle_t *spot_,
                          zlink_subscribe_handler_fn handler_,
                          void *userdata_);
int spot_node_install_handler (zlink::spot_node_t *node_,
                               zlink_subscribe_handler_fn handler_,
                               void *userdata_);
int spot_install_recv_handler (spot_handle_t *spot_,
                               zlink_subscribe_handler_fn handler_,
                               void *userdata_);
int spot_node_install_recv_handler (zlink::spot_node_t *node_,
                                    zlink_subscribe_handler_fn handler_,
                                    void *userdata_);
int spot_install_send_ready_handler (spot_handle_t *spot_,
                                     zlink_send_ready_handler_fn handler_,
                                     void *userdata_);
int spot_node_install_send_ready_handler (zlink::spot_node_t *node_,
                                          zlink_send_ready_handler_fn handler_,
                                          void *userdata_);

#endif
