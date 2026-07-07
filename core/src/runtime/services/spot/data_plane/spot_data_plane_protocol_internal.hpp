/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_DATA_PLANE_PROTOCOL_INTERNAL_HPP_INCLUDED__
#define __ZLINK_SPOT_DATA_PLANE_PROTOCOL_INTERNAL_HPP_INCLUDED__

#include "services/spot/data_plane/spot_data_plane_protocol_state.hpp"
#include "services/spot/data_plane/spot_data_plane_runtime_state.hpp"
#include "services/spot/data_plane/spot_mesh_peer_state.hpp"

#include <string>
#include <vector>

namespace zlink
{
class socket_base_t;
class socket_poller_t;
class spot_node_t;
struct spot_runtime_t;

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
}

#endif
