/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_DATA_PLANE_FORWARDING_INTERNAL_HPP_INCLUDED__
#define __ZLINK_SPOT_DATA_PLANE_FORWARDING_INTERNAL_HPP_INCLUDED__

#include "services/spot/common/spot_message_parts_internal.hpp"
#include "services/spot/data_plane/spot_data_plane_protocol_state.hpp"
#include "services/spot/data_plane/spot_data_plane_runtime_state.hpp"

#include <string>

namespace zlink
{
class socket_base_t;
struct spot_runtime_t;

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
}

#endif
