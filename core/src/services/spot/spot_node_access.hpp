/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_NODE_ACCESS_HPP_INCLUDED__
#define __ZLINK_SPOT_NODE_ACCESS_HPP_INCLUDED__

#include <zlink.h>

#include <string>
#include <vector>

struct spot_handle_t;

namespace zlink
{
class ctx_t;
class discovery_t;
class mutex_t;
class socket_base_t;
struct spot_runtime_t;
class spot_internal_receiver_t;
class spot_node_t;

enum spot_node_monitor_subject_t
{
    spot_node_monitor_subject_none = 0,
    spot_node_monitor_subject_pub,
    spot_node_monitor_subject_internal_receiver
};

struct spot_node_access_t
{
    static void *create (ctx_t *ctx_);
    static ctx_t *ctx (spot_node_t *node_);
    static mutex_t &sync (spot_node_t *node_);
    static spot_node_t *from_handle (void *node_);
    static int bind (spot_node_t *node_, const char *endpoint_);
    static int connect_peer (spot_node_t *node_, const char *peer_endpoint_);
    static int disconnect_peer (spot_node_t *node_,
                                const char *peer_endpoint_);
    static int set_node_option (spot_node_t *node_,
                                zlink_spot_node_option_t option_,
                                const void *optval_,
                                size_t optvallen_);
    static int get_node_option (spot_node_t *node_,
                                zlink_spot_node_option_t option_,
                                void *optval_,
                                size_t *optvallen_);
    static int begin_close_or_fail_busy (spot_node_t *node_);
    static void cancel_close (spot_node_t *node_);
    static int destroy (spot_node_t *node_);
    static void delete_handle (spot_node_t *node_);
    static int status_snapshot (spot_node_t *node_,
                                zlink_spot_node_status_t *out_);
    static std::string summary_service_name (spot_node_t *node_);
    static socket_base_t *select_service_router (spot_node_t *node_,
                                                 const std::string &service_name_);
    static socket_base_t *service_pub_socket (spot_node_t *node_,
                                              const std::string &service_name_);
    static int service_subscribe_recv (spot_node_t *node_,
                                       zlink_routing_id_t *source_rid_out_,
                                       zlink_msg_t **parts_out_,
                                       size_t *part_count_out_,
                                       char *service_name_out_,
                                       size_t *service_name_len_out_,
                                       char *topic_id_out_,
                                       size_t *topic_id_len_out_,
                                       zlink_recv_flags_t flags_);
    static int service_subscription_event_recv (
      spot_node_t *node_,
      zlink_routing_id_t *source_rid_out_,
      int *subscribed_out_,
      char *service_name_out_,
      size_t *service_name_len_out_,
      char *topic_id_out_,
      size_t *topic_id_len_out_,
      zlink_recv_flags_t flags_);
    static int peers_snapshot (
      spot_node_t *node_,
      const zlink_spot_node_peer_filter_t *filter_,
      std::vector<zlink_spot_node_peer_entry_t> *out_);
    static int subjects_snapshot (
      spot_node_t *node_,
      const zlink_spot_node_subject_filter_t *filter_,
      std::vector<zlink_spot_node_subject_entry_t> *out_);
    static int attach_discovery (spot_node_t *node_, void *discovery_);
    static int attach_channel_dealer (spot_node_t *node_,
                                      void *discovery_,
                                      void *dealer_);
    static int attach_channel_dealer_manual (spot_node_t *node_,
                                             const char *channel_name_,
                                             void *dealer_);
    static int attach_pub_ingress (spot_node_t *node_, void *pub_);
    static int try_register_spot_facade (spot_node_t *node_,
                                         spot_handle_t *spot_);
    static void unregister_spot_facade (spot_node_t *node_,
                                        spot_handle_t *spot_);
    static void *monitor_open (spot_node_t *node_,
                               zlink_spot_role_t role_,
                               int events_,
                               void **snapshot_subject_out_,
                               spot_node_monitor_subject_t *subject_kind_out_);
    static spot_runtime_t *runtime (spot_node_t *node_);
    static void track_owned_socket (spot_node_t *node_, socket_base_t *socket_);
    static void untrack_owned_socket (spot_node_t *node_,
                                     const socket_base_t *socket_);
    static spot_internal_receiver_t *ensure_internal_receiver (spot_node_t *node_);
    static spot_internal_receiver_t *internal_receiver (spot_node_t *node_);
    static void wake_control_task (spot_node_t *node_);
    static void schedule_subscription_replay (spot_node_t *node_);
};
}

#endif
