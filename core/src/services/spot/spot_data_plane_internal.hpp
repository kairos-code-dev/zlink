/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_DATA_PLANE_INTERNAL_HPP_INCLUDED__
#define __ZLINK_SPOT_DATA_PLANE_INTERNAL_HPP_INCLUDED__

#include <zlink.h>

#include <map>
#include <set>
#include <string>
#include <vector>

namespace zlink
{
class socket_base_t;
class spot_node_t;
struct spot_runtime_t;

struct spot_data_plane_protocol_state_t
{
    std::map<std::string, std::string> peer_ctrl_endpoints;
    std::map<std::string, std::set<std::string> > peer_ready_filters;
    std::map<std::string, std::map<std::string, std::set<std::string> > >
      outbound_ready_filters;
};

inline bool is_websocket_transport (const std::string &endpoint_)
{
    return endpoint_.compare (0, 5, "ws://") == 0
           || endpoint_.compare (0, 6, "wss://") == 0;
}

inline bool is_secure_transport (const std::string &endpoint_)
{
    return endpoint_.compare (0, 6, "tls://") == 0
           || endpoint_.compare (0, 6, "wss://") == 0;
}

inline bool is_wss_transport (const std::string &endpoint_)
{
    return endpoint_.compare (0, 6, "wss://") == 0;
}

inline int resolve_mesh_pub_sndhwm_default (const std::string &endpoint_,
                                            unsigned int ready_peers_)
{
    const bool secure = is_secure_transport (endpoint_);
    const bool wss = is_wss_transport (endpoint_);

    if (ready_peers_ == 0)
        return 64;
    if (ready_peers_ == 1)
        return 768;
    if (wss)
        return 256;
    if (!secure)
        return 64;
    return 768;
}

inline bool should_refresh_mesh_pub_budget (const std::string &endpoint_,
                                            unsigned int previous_ready_peers_,
                                            unsigned int next_ready_peers_)
{
    const int previous_budget =
      resolve_mesh_pub_sndhwm_default (endpoint_, previous_ready_peers_);
    const int next_budget =
      resolve_mesh_pub_sndhwm_default (endpoint_, next_ready_peers_);
    return previous_budget != next_budget;
}

inline bool sync_monitor_ready_endpoint (
  std::set<std::string> *endpoints_, const zlink_monitor_event_t &raw_)
{
    if (!endpoints_ || raw_.remote_addr[0] == '\0')
        return false;
    if (raw_.event != ZLINK_EVENT_CONNECTION_READY_CHANGED)
        return endpoints_->erase (raw_.remote_addr) != 0;
    if (raw_.value <= 0)
        return endpoints_->erase (raw_.remote_addr) != 0;
    return endpoints_->insert (raw_.remote_addr).second;
}

inline bool sync_monitor_connected_endpoint (
  std::set<std::string> *endpoints_, const zlink_monitor_event_t &raw_)
{
    if (!endpoints_ || raw_.remote_addr[0] == '\0')
        return false;
    if (raw_.event == ZLINK_EVENT_DISCONNECTED)
        return endpoints_->erase (raw_.remote_addr) != 0;
    if (raw_.event == ZLINK_EVENT_CONNECTED)
        return endpoints_->insert (raw_.remote_addr).second;
    if (raw_.event == ZLINK_EVENT_CONNECTION_READY_CHANGED && raw_.value > 0)
        return endpoints_->insert (raw_.remote_addr).second;
    return false;
}

inline bool apply_monitor_ready_peer_count (uint32_t *ready_peer_count_out_,
                                            const zlink_monitor_event_t &raw_)
{
    if (!ready_peer_count_out_
        || raw_.event != ZLINK_EVENT_CONNECTION_READY_CHANGED)
        return false;

    const uint32_t next_ready_count =
      raw_.value > 0 ? static_cast<uint32_t> (raw_.value) : 0u;
    if (*ready_peer_count_out_ == next_ready_count)
        return false;
    *ready_peer_count_out_ = next_ready_count;
    return true;
}

inline uint32_t clamp_ready_peer_count (uint32_t ready_count_,
                                        uint32_t connected_peer_count_)
{
    return ready_count_ < connected_peer_count_ ? ready_count_
                                                : connected_peer_count_;
}

struct spot_data_plane_runtime_state_t
{
    spot_data_plane_runtime_state_t ();

    socket_base_t *ctrl;
    socket_base_t *mesh_pub;
    socket_base_t *mesh_xsub;
    socket_base_t *mesh_xsub_monitor;
    socket_base_t *peer_ctrl_pub;
    socket_base_t *peer_ctrl_sub;
    socket_base_t *ingress;
    socket_base_t *fanout;
    int current_mesh_pub_sndhwm;
    uint64_t last_mesh_pub_budget_version;
    std::string last_mesh_pub_bound_endpoint;
};

struct spot_data_plane_protocol_t
{
    static int recv_ascii_command (socket_base_t *socket_,
                                   std::vector<std::string> *frames_);
    static int send_subscription_update (socket_base_t *socket_,
                                         const std::string &raw_filter_,
                                         bool subscribe_);
    static int send_errno_reply (socket_base_t *socket_, int error_);
    static int send_ok_reply (socket_base_t *socket_);
    static int recv_and_process_ctrl_messages (
      socket_base_t *ctrl_sub_,
      spot_node_t *node_,
      spot_data_plane_protocol_state_t *state_);
    static int recv_and_dispatch_mesh_xsub (
      socket_base_t *mesh_xsub_,
      socket_base_t *fanout_,
      socket_base_t *peer_ctrl_pub_,
      spot_node_t *node_,
      spot_data_plane_protocol_state_t *state_);
    static int handle_ctrl_command (
      socket_base_t *ctrl_,
      spot_node_t *node_,
      spot_runtime_t *runtime_,
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
    static uint64_t resolve_bootstrap_broadcast_interval_ms (
      const spot_runtime_t *runtime_,
      bool bootstrap_ready_);
    static void sync_mesh_xsub_connected_endpoint (
      spot_runtime_t *runtime_, const zlink_monitor_event_t &raw_);
    static void clear_mesh_xsub_connected_endpoints (spot_runtime_t *runtime_);
    static void clear_snapshot_sources (
      spot_node_t *node_, spot_data_plane_protocol_state_t *state_);
};

struct spot_data_plane_forwarder_t
{
    static void pump_socket_commands (socket_base_t *socket_);
    static int resolve_internal_hwm_override (const char *env_name_,
                                              int default_value_);
    static int recv_and_forward_ingress (socket_base_t *src_,
                                         socket_base_t *mesh_pub_,
                                         socket_base_t *fanout_,
                                         const spot_node_t *node_);
};
}

#endif
