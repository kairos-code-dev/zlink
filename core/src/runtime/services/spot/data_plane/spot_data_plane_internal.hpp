/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_DATA_PLANE_INTERNAL_HPP_INCLUDED__
#define __ZLINK_SPOT_DATA_PLANE_INTERNAL_HPP_INCLUDED__

#include <zlink.h>

#include "services/spot/common/spot_message_parts_internal.hpp"
#include "services/spot/data_plane/spot_data_plane_protocol_state.hpp"
#include "services/spot/data_plane/spot_data_plane_runtime_state.hpp"
#include "services/spot/data_plane/spot_mesh_peer_state.hpp"
#include "core/signaler.hpp"
#include "utils/mutex.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace zlink
{
class socket_base_t;
class spot_node_t;
struct spot_runtime_t;
class socket_poller_t;

int spot_data_plane_configure_runtime_sockets (spot_runtime_t *runtime_,
                                               spot_data_plane_runtime_state_t *state_);

struct spot_data_plane_pending_t
{
    static bool
    queue_has_room (size_t current_bytes_, size_t hard_limit_bytes_, size_t message_bytes_);
    static int copy_msg_parts_to_owned (const spot_owned_msg_parts_t &src_,
                                        spot_owned_msg_parts_t *dst_);
    static int resolve_fanout_hwm (spot_runtime_t *runtime_);
    static void release_local_pending_ref (spot_data_plane_runtime_state_t *state_,
                                           uint64_t message_id_);
    static void drop_local_target_state (spot_data_plane_runtime_state_t *state_,
                                         uint64_t attachment_id_);
    static void release_mesh_pending_ref (spot_data_plane_runtime_state_t *state_,
                                          uint64_t message_id_);
    static void close_remote_target_socket (spot_runtime_t *runtime_,
                                            const std::string &route_endpoint_,
                                            socket_base_t *&socket_);
    static void drop_remote_target_state (spot_runtime_t *runtime_,
                                          spot_data_plane_runtime_state_t *state_,
                                          const std::string &endpoint_);
    static bool
    enqueue_local_target_message (spot_data_plane_runtime_state_t *state_,
                                  spot_data_plane_runtime_state_t::local_target_state_t *target_,
                                  const std::string &topic_,
                                  const spot_owned_msg_parts_t &parts_,
                                  uint64_t *message_id_inout_,
                                  size_t precomputed_encoded_bytes_ = 0);
    static bool
    enqueue_mesh_pending (spot_data_plane_runtime_state_t *state_,
                          spot_data_plane_runtime_state_t::remote_target_state_t *target_,
                          const std::string &topic_,
                          const spot_owned_msg_parts_t &parts_,
                          uint64_t *message_id_inout_);
    static bool enqueue_mesh_broadcast_pending (spot_data_plane_runtime_state_t *state_,
                                                const std::string &topic_,
                                                const spot_owned_msg_parts_t &parts_,
                                                uint64_t *message_id_inout_,
                                                size_t precomputed_encoded_bytes_ = 0);
    static bool stage_publish_message (
      std::deque<spot_data_plane_runtime_state_t::staged_publish_entry_t> *queue_,
      const std::string &topic_,
      const spot_owned_msg_parts_t &parts_,
      bool need_local_,
      bool need_mesh_);
};

struct spot_data_plane_protocol_t
{
    static int recv_ascii_command (socket_base_t *socket_, std::vector<std::string> *frames_);
    static int send_subscription_update (socket_base_t *socket_,
                                         const std::string &raw_filter_,
                                         bool subscribe_);
    static int send_errno_reply (socket_base_t *socket_, int error_);
    static int send_ok_reply (socket_base_t *socket_);
    static int recv_and_process_ctrl_messages (socket_base_t *ctrl_sub_,
                                               spot_node_t *node_,
                                               spot_data_plane_protocol_state_t *state_);
    static int recv_and_dispatch_mesh_xsub (socket_base_t *mesh_xsub_,
                                            socket_base_t *peer_ctrl_pub_,
                                            spot_runtime_t *runtime_,
                                            spot_data_plane_runtime_state_t *runtime_state_,
                                            spot_node_t *node_,
                                            spot_data_plane_protocol_state_t *state_);
    static int handle_ctrl_command (socket_base_t *ctrl_,
                                    spot_node_t *node_,
                                    spot_runtime_t *runtime_,
                                    socket_poller_t *poller_,
                                    socket_base_t *mesh_pub_,
                                    socket_base_t *mesh_xsub_,
                                    socket_base_t *peer_ctrl_pub_,
                                    socket_base_t *peer_ctrl_sub_,
                                    const std::vector<std::string> &frames_,
                                    spot_data_plane_protocol_state_t *state_,
                                    bool *running_out_);
    static int publish_bootstrap_descriptor (socket_base_t *mesh_pub_,
                                             spot_node_t *node_,
                                             spot_runtime_t *runtime_);
    static bool should_publish_bootstrap_descriptor (const spot_runtime_t *runtime_,
                                                     bool bootstrap_ready_,
                                                     uint64_t last_published_peer_version_);
    static uint64_t resolve_bootstrap_broadcast_interval_ms (const spot_runtime_t *runtime_,
                                                             bool bootstrap_ready_);
    static void sync_mesh_connected_endpoint (spot_runtime_t *runtime_,
                                              const zlink_monitor_event_t &raw_);
    static void clear_mesh_connected_endpoints (spot_runtime_t *runtime_);
    static void clear_snapshot_sources (spot_node_t *node_,
                                        spot_data_plane_protocol_state_t *state_);
};

struct spot_data_plane_forwarder_t
{
    static void pump_socket_commands (socket_base_t *socket_);
    static void sync_local_fanout_targets (spot_runtime_t *runtime_,
                                           spot_data_plane_runtime_state_t *state_);
    static void sync_remote_mesh_targets (spot_runtime_t *runtime_,
                                          spot_data_plane_runtime_state_t *state_,
                                          const spot_data_plane_protocol_state_t *protocol_state_);
    static void drop_remote_mesh_target (spot_runtime_t *runtime_,
                                         spot_data_plane_runtime_state_t *state_,
                                         const std::string &endpoint_);
    static void update_pending_queue_limits (spot_runtime_t *runtime_,
                                             spot_data_plane_runtime_state_t *state_);
    static void refresh_poller_interest (spot_data_plane_runtime_state_t *state_);
    static int forward_local_fanout (spot_runtime_t *runtime_,
                                     spot_data_plane_runtime_state_t *state_,
                                     const std::string &topic_,
                                     const spot_owned_msg_parts_t &parts_,
                                     size_t precomputed_encoded_bytes_ = 0);
    static int forward_mesh_pub (spot_runtime_t *runtime_,
                                 spot_data_plane_runtime_state_t *state_,
                                 const std::string &topic_,
                                 const spot_owned_msg_parts_t &parts_,
                                 size_t precomputed_encoded_bytes_ = 0);
    static int stage_message (spot_data_plane_runtime_state_t *state_,
                              const std::string &topic_,
                              const spot_owned_msg_parts_t &parts_,
                              bool source_mesh_,
                              bool need_local_,
                              bool need_mesh_);
    static int enqueue_publish_ingress (spot_runtime_t *runtime_,
                                        const char *topic_,
                                        zlink_msg_t *parts_,
                                        size_t part_count_,
                                        zlink_send_flags_t flags_,
                                        int sndtimeo_ms_);
    static int drain_publish_ingress_queue (spot_runtime_t *runtime_,
                                            spot_data_plane_runtime_state_t *state_);
    static int drain_pub_ingress_socket (spot_runtime_t *runtime_,
                                         spot_data_plane_runtime_state_t *state_);
    static int flush_local_fanout_pending (spot_runtime_t *runtime_,
                                           spot_data_plane_runtime_state_t *state_,
                                           socket_base_t *relay_socket_ = NULL);
    static int flush_mesh_pub_pending (spot_runtime_t *runtime_,
                                       spot_data_plane_runtime_state_t *state_,
                                       socket_base_t *sender_socket_ = NULL);
    static int flush_staged_messages (spot_runtime_t *runtime_,
                                      spot_data_plane_runtime_state_t *state_);
};

struct spot_data_plane_poller_interest_t
{
    static void refresh_fixed (spot_data_plane_runtime_state_t *state_);
    static void
    refresh_local_target (spot_data_plane_runtime_state_t *state_,
                          spot_data_plane_runtime_state_t::local_target_state_t *target_);
    static void
    refresh_remote_target (spot_data_plane_runtime_state_t *state_,
                           spot_data_plane_runtime_state_t::remote_target_state_t *target_);
    static void refresh_all (spot_data_plane_runtime_state_t *state_);
};
}

#endif
