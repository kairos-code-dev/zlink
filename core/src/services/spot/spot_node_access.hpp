/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_NODE_ACCESS_HPP_INCLUDED__
#define __ZLINK_SPOT_NODE_ACCESS_HPP_INCLUDED__

#include <zlink.h>

#include <map>
#include <set>
#include <string>
#include <vector>
#include <memory>

struct actor_handle_t;
struct spot_handle_t;
struct spot_logical_state_t;

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
    static void *create (ctx_t *ctx_, zlink_spot_node_mode_t mode_);
    static ctx_t *ctx (spot_node_t *node_);
    static mutex_t &sync (spot_node_t *node_);
    static spot_node_t *from_handle (void *node_);
    static int bind (spot_node_t *node_, const char *endpoint_);
    static int connect_peer (spot_node_t *node_, const char *peer_endpoint_);
    static int disconnect_peer (spot_node_t *node_,
                                const char *peer_endpoint_);
    static int disconnect_peer_rid (spot_node_t *node_,
                                    const zlink_routing_id_t *target_node_rid_);
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
    static bool has_active_peers (spot_node_t *node_);
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
    static int internal_sockets_snapshot (
      spot_node_t *node_,
      const zlink_spot_node_socket_snapshot_filter_t *filter_,
      std::vector<zlink_spot_node_socket_snapshot_entry_t> *out_);
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
    static bool is_last_spot_facade_for_logical_state (spot_node_t *node_,
                                                       spot_handle_t *spot_);
    static std::shared_ptr<spot_logical_state_t> create_user_spot_state (
      spot_node_t *node_);
    static std::shared_ptr<spot_logical_state_t> entry_spot_state (
      spot_node_t *node_);
    static std::shared_ptr<spot_logical_state_t> lookup_spot_state (
      spot_node_t *node_, const zlink_routing_id_t *spot_rid_);
    static int update_spot_routing_id (spot_node_t *node_,
                                       spot_handle_t *spot_,
                                       const void *data_,
                                       size_t size_);
    static spot_runtime_t *runtime (spot_node_t *node_);
    static zlink_spot_node_mode_t mode (spot_node_t *node_);
    static bool pubsub_enabled (spot_node_t *node_);
    static bool routed_enabled (spot_node_t *node_);
    static bool is_shutting_down (spot_node_t *node_);
    static socket_base_t *create_socket (spot_node_t *node_,
                                         int socket_type_);
    static void track_owned_socket (spot_node_t *node_, socket_base_t *socket_);
    static void untrack_owned_socket (spot_node_t *node_,
                                     const socket_base_t *socket_);
    static spot_internal_receiver_t *ensure_internal_receiver (spot_node_t *node_);
    static spot_internal_receiver_t *internal_receiver (spot_node_t *node_);
    static std::set<actor_handle_t *> &actor_handles (spot_node_t *node_);
    static std::map<std::string, actor_handle_t *> &actors_by_id (
      spot_node_t *node_);
    static uint64_t &next_actor_generation (spot_node_t *node_);
    static void wake_control_task (spot_node_t *node_);
    static void schedule_subscription_replay (spot_node_t *node_);
    static void snapshot_active_peer_endpoints (
      spot_node_t *node_, std::set<std::string> *out_);
    static void snapshot_tls_client_config (spot_node_t *node_,
                                            std::string *ca_out_,
                                            std::string *host_out_,
                                            int *trust_system_out_);
    static void snapshot_tls_server_config (spot_node_t *node_,
                                            std::string *cert_out_,
                                            std::string *key_out_);
    static void mark_bound_endpoint_and_server_tls_locked (
      spot_node_t *node_, const std::string &bound_endpoint_);
    static void mark_mesh_client_tls_locked (spot_node_t *node_);
    static int apply_tls_server (spot_node_t *node_,
                                 socket_base_t *socket_,
                                 const std::string &cert_,
                                 const std::string &key_);
    static int apply_tls_client (spot_node_t *node_,
                                 socket_base_t *socket_,
                                 const std::string &ca_cert_,
                                 const std::string &hostname_,
                                 int trust_system_);
    static bool recv_ctrl_reply (socket_base_t *socket_, int *out_errno_);
    static int close_owned_socket (spot_node_t *node_,
                                   socket_base_t *&socket_,
                                   int timeout_ms_);
    static int close_owned_socket_and_wait (spot_node_t *node_,
                                            socket_base_t *&socket_,
                                            int timeout_ms_);
    static int send_internal_subscription_update (spot_node_t *node_,
                                                  const std::string &raw_filter_,
                                                  bool subscribe_);
};
}

#endif
