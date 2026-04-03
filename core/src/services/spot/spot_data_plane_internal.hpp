/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_DATA_PLANE_INTERNAL_HPP_INCLUDED__
#define __ZLINK_SPOT_DATA_PLANE_INTERNAL_HPP_INCLUDED__

#include <zlink.h>

#include "utils/mutex.hpp"

#include <atomic>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace zlink
{
class socket_base_t;
class socket_poller_t;
class spot_node_t;
struct spot_runtime_t;

struct spot_data_plane_protocol_state_t
{
    std::map<std::string, std::string> peer_ctrl_endpoints;
    std::map<std::string, std::set<std::string> > peer_ready_filters;
    std::map<std::string, std::map<std::string, std::set<std::string> > >
      outbound_ready_filters;
};

struct spot_mesh_peer_state_t
{
    spot_mesh_peer_state_t () :
        version (0),
        budget_version (0),
        mesh_pub_ready_peer_count (0),
        connected_ready_peer_count (0)
    {
    }

    mutable mutex_t sync;
    std::atomic<uint64_t> version;
    std::atomic<uint64_t> budget_version;
    std::atomic<uint32_t> mesh_pub_ready_peer_count;
    std::atomic<uint32_t> connected_ready_peer_count;
    std::set<std::string> connected_endpoints;
};

inline bool sync_monitor_ready_endpoint (
  std::set<std::string> *endpoints_, const zlink_monitor_event_t &raw_)
{
    if (!endpoints_ || raw_.remote_addr[0] == '\0')
        return false;
    if (raw_.event == ZLINK_EVENT_DISCONNECTED)
        return endpoints_->erase (raw_.remote_addr) != 0;
    if (raw_.event != ZLINK_EVENT_CONNECTION_READY)
        return false;
    return endpoints_->insert (raw_.remote_addr).second;
}

inline bool sync_monitor_connected_endpoint (
  std::set<std::string> *endpoints_, const zlink_monitor_event_t &raw_)
{
    if (!endpoints_ || raw_.remote_addr[0] == '\0')
        return false;
    if (raw_.event == ZLINK_EVENT_DISCONNECTED)
        return endpoints_->erase (raw_.remote_addr) != 0;
    if (raw_.event == ZLINK_EVENT_CONNECTION_READY)
        return endpoints_->insert (raw_.remote_addr).second;
    return false;
}

inline bool apply_monitor_ready_peer_count (uint32_t *ready_peer_count_out_,
                                            size_t endpoint_count_)
{
    if (!ready_peer_count_out_)
        return false;
    const uint32_t next_ready_count =
      static_cast<uint32_t> (endpoint_count_);
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

inline void snapshot_connected_mesh_peer_endpoints (
  const spot_mesh_peer_state_t *state_, std::set<std::string> *out_)
{
    if (!out_)
        return;
    out_->clear ();
    if (!state_)
        return;
    scoped_lock_t lock (const_cast<mutex_t &> (state_->sync));
    *out_ = state_->connected_endpoints;
}

inline bool sync_mesh_peer_monitor_state (
  spot_mesh_peer_state_t *state_,
  const zlink_monitor_event_t &raw_,
  bool *endpoint_membership_changed_out_ = NULL)
{
    if (!state_ || raw_.remote_addr[0] == '\0')
        return false;

    scoped_lock_t lock (state_->sync);
    bool endpoint_membership_changed =
      sync_monitor_connected_endpoint (&state_->connected_endpoints, raw_);
    bool changed = endpoint_membership_changed;
    if (raw_.event == ZLINK_EVENT_DISCONNECTED
        && state_->connected_endpoints.empty ()
        && state_->connected_ready_peer_count.load (std::memory_order_acquire)
             != 0) {
        state_->connected_ready_peer_count.store (0,
                                                  std::memory_order_release);
        changed = true;
    }

    uint32_t ready_peer_count =
      state_->connected_ready_peer_count.load (std::memory_order_acquire);
    if (apply_monitor_ready_peer_count (&ready_peer_count,
                                        state_->connected_endpoints.size ())) {
        state_->connected_ready_peer_count.store (ready_peer_count,
                                                  std::memory_order_release);
        changed = true;
    }

    if (changed)
        state_->version.fetch_add (1, std::memory_order_acq_rel);
    if (endpoint_membership_changed_out_)
        *endpoint_membership_changed_out_ = endpoint_membership_changed;
    return changed;
}

inline bool clear_mesh_peer_monitor_state (spot_mesh_peer_state_t *state_)
{
    if (!state_)
        return false;

    scoped_lock_t lock (state_->sync);
    if (!state_->connected_endpoints.empty ()) {
        state_->connected_endpoints.clear ();
        state_->connected_ready_peer_count.store (0,
                                                  std::memory_order_release);
        state_->version.fetch_add (1, std::memory_order_acq_rel);
        return true;
    }
    state_->connected_ready_peer_count.store (0, std::memory_order_release);
    return false;
}

inline bool remove_connected_mesh_peer_endpoint (spot_mesh_peer_state_t *state_,
                                                 const std::string &endpoint_)
{
    if (!state_ || endpoint_.empty ())
        return false;

    scoped_lock_t lock (state_->sync);
    const std::set<std::string>::iterator it =
      state_->connected_endpoints.find (endpoint_);
    if (it == state_->connected_endpoints.end ())
        return false;

    state_->connected_endpoints.erase (it);
    if (state_->connected_endpoints.empty ())
        state_->connected_ready_peer_count.store (0,
                                                  std::memory_order_release);
    state_->version.fetch_add (1, std::memory_order_acq_rel);
    return true;
}

inline uint32_t connected_ready_peer_count (
  const spot_mesh_peer_state_t *state_)
{
    return state_ ? state_->connected_ready_peer_count.load (
                     std::memory_order_acquire)
                  : 0u;
}

inline uint32_t mesh_pub_ready_peer_count (const spot_mesh_peer_state_t *state_)
{
    return state_ ? state_->mesh_pub_ready_peer_count.load (
                     std::memory_order_acquire)
                  : 0u;
}

inline uint64_t mesh_peer_version (const spot_mesh_peer_state_t *state_)
{
    return state_ ? state_->version.load (std::memory_order_acquire) : 0u;
}

inline uint64_t mesh_pub_budget_version (const spot_mesh_peer_state_t *state_)
{
    return state_ ? state_->budget_version.load (std::memory_order_acquire)
                  : 0u;
}

inline void reset_mesh_pub_budget_state (spot_mesh_peer_state_t *state_)
{
    if (!state_)
        return;

    state_->mesh_pub_ready_peer_count.store (0, std::memory_order_release);
    state_->budget_version.fetch_add (1, std::memory_order_acq_rel);
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
    socket_poller_t *poller;
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
    static bool should_publish_bootstrap_descriptor (
      const spot_runtime_t *runtime_,
      bool bootstrap_ready_,
      uint64_t last_published_peer_version_);
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
