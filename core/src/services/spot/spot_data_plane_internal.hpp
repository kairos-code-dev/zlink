/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_DATA_PLANE_INTERNAL_HPP_INCLUDED__
#define __ZLINK_SPOT_DATA_PLANE_INTERNAL_HPP_INCLUDED__

#include <zlink.h>

#include "services/spot/spot_message_parts_internal.hpp"
#include "utils/mutex.hpp"

#include <atomic>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
    std::set<std::string> outbound_subscription_filters;
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

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif

inline uint32_t connected_ready_peer_count (
  const spot_mesh_peer_state_t *state_)
{
    if (!state_)
        return 0u;

    return state_->connected_ready_peer_count.load (
      std::memory_order_acquire);
}

inline uint32_t mesh_pub_ready_peer_count (const spot_mesh_peer_state_t *state_)
{
    if (!state_)
        return 0u;

    return state_->mesh_pub_ready_peer_count.load (
      std::memory_order_acquire);
}

inline uint64_t mesh_peer_version (const spot_mesh_peer_state_t *state_)
{
    if (!state_)
        return 0u;

    return state_->version.load (std::memory_order_acquire);
}

inline uint64_t mesh_pub_budget_version (const spot_mesh_peer_state_t *state_)
{
    if (!state_)
        return 0u;

    return state_->budget_version.load (std::memory_order_acquire);
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

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

    struct publish_pending_entry_t
    {
        publish_pending_entry_t () :
            message_id (0),
            encoded_bytes (0),
            remaining_targets (0)
        {
        }

        uint64_t message_id;
        std::string topic;
        spot_owned_msg_parts_t parts;
        size_t encoded_bytes;
        uint32_t remaining_targets;
    };

    struct local_target_state_t
    {
        local_target_state_t () :
            attachment_id (0),
            relay_socket (NULL),
            pollout_armed (false)
        {
        }

        uint64_t attachment_id;
        socket_base_t *relay_socket;
        bool pollout_armed;
        std::deque<uint64_t> pending_message_ids;
    };

    struct staged_publish_entry_t
    {
        staged_publish_entry_t () :
            encoded_bytes (0),
            need_local (false),
            need_mesh (false)
        {
        }

        std::string topic;
        spot_owned_msg_parts_t parts;
        size_t encoded_bytes;
        bool need_local;
        bool need_mesh;
    };

    struct remote_target_state_t
    {
        remote_target_state_t () :
            sender_socket (NULL),
            pollout_armed (false)
        {
        }

        std::string endpoint;
        std::string route_endpoint;
        socket_base_t *sender_socket;
        bool pollout_armed;
        std::deque<uint64_t> pending_message_ids;
    };

    struct mesh_pub_budget_state_t
    {
        mesh_pub_budget_state_t () :
            current_sndhwm (0),
            last_budget_version (UINT64_MAX)
        {
        }

        int current_sndhwm;
        uint64_t last_budget_version;
        std::string last_bound_endpoint;
    };

    struct local_fanout_state_t
    {
        typedef std::unordered_map<uint64_t, publish_pending_entry_t>
          pending_message_map_t;
        typedef std::unordered_map<uint64_t, local_target_state_t>
          target_map_t;

        local_fanout_state_t () :
            pending_bytes (0),
            pending_pause_threshold (0),
            pending_resume_threshold (0),
            pending_hard_limit (0),
            pending_message_count_hard_limit (
              ZLINK_SPOT_NODE_SUB_QUEUE_HARD_LIMIT_DFLT)
        {
        }

        size_t pending_bytes;
        size_t pending_pause_threshold;
        size_t pending_resume_threshold;
        size_t pending_hard_limit;
        size_t pending_message_count_hard_limit;
        pending_message_map_t pending_messages;
        target_map_t targets;
        std::unordered_map<socket_base_t *, uint64_t> target_by_socket;
        std::unordered_set<uint64_t> disconnected_targets;
    };

    struct remote_mesh_state_t
    {
        typedef std::unordered_map<uint64_t, publish_pending_entry_t>
          pending_message_map_t;
        typedef std::unordered_map<std::string, remote_target_state_t>
          target_map_t;

        remote_mesh_state_t () :
            pollout_armed (false),
            pending_bytes (0),
            pending_pause_threshold (0),
            pending_resume_threshold (0),
            pending_hard_limit (0)
        {
        }

        bool pollout_armed;
        size_t pending_bytes;
        size_t pending_pause_threshold;
        size_t pending_resume_threshold;
        size_t pending_hard_limit;
        pending_message_map_t pending_messages;
        std::deque<uint64_t> broadcast_pending_message_ids;
        target_map_t targets;
        std::unordered_map<socket_base_t *, std::string> target_by_socket;
    };

    struct staged_publish_state_t
    {
        std::deque<staged_publish_entry_t> ingress_messages;
        std::deque<staged_publish_entry_t> mesh_messages;
    };

    struct poller_interest_state_t
    {
        poller_interest_state_t () :
            ingress_pollin_paused (false),
            mesh_xsub_pollin_paused (false),
            ingress_pollin_armed (true),
            mesh_xsub_pollin_armed (true)
        {
        }

        bool ingress_pollin_paused;
        bool mesh_xsub_pollin_paused;
        bool ingress_pollin_armed;
        bool mesh_xsub_pollin_armed;
    };

    socket_base_t *ctrl;
    socket_base_t *mesh_pub;
    socket_base_t *mesh_xsub;
    socket_base_t *mesh_xsub_monitor;
    socket_base_t *peer_ctrl_pub;
    socket_base_t *peer_ctrl_sub;
    socket_base_t *external_router;
    socket_base_t *internal_router;
    socket_base_t *ingress;
    socket_base_t *fanout;
    uint64_t next_pending_message_id;
    uint64_t last_attachment_version;
    bool runtime_sockets_nodelay_applied;
    mesh_pub_budget_state_t mesh_pub_budget;
    poller_interest_state_t interest;
    local_fanout_state_t local_fanout;
    remote_mesh_state_t remote_mesh;
    staged_publish_state_t staged;
    socket_poller_t *poller;
};

struct spot_data_plane_pending_t
{
    static bool queue_has_room (size_t current_bytes_,
                                size_t hard_limit_bytes_,
                                size_t message_bytes_);
    static int copy_msg_parts_to_owned (const spot_owned_msg_parts_t &src_,
                                        spot_owned_msg_parts_t *dst_);
    static int resolve_fanout_budget_bytes (spot_runtime_t *runtime_);
    static void release_local_pending_ref (
      spot_data_plane_runtime_state_t *state_, uint64_t message_id_);
    static void drop_local_target_state (
      spot_data_plane_runtime_state_t *state_, uint64_t attachment_id_);
    static void release_mesh_pending_ref (
      spot_data_plane_runtime_state_t *state_, uint64_t message_id_);
    static void close_remote_target_socket (spot_runtime_t *runtime_,
                                            const std::string &route_endpoint_,
                                            socket_base_t *&socket_);
    static void drop_remote_target_state (spot_runtime_t *runtime_,
                                          spot_data_plane_runtime_state_t *state_,
                                          const std::string &endpoint_);
    static bool enqueue_local_target_message (
      spot_data_plane_runtime_state_t *state_,
      spot_data_plane_runtime_state_t::local_target_state_t *target_,
      const std::string &topic_,
      const spot_owned_msg_parts_t &parts_,
      uint64_t *message_id_inout_);
    static bool enqueue_mesh_pending (
      spot_data_plane_runtime_state_t *state_,
      spot_data_plane_runtime_state_t::remote_target_state_t *target_,
      const std::string &topic_,
      const spot_owned_msg_parts_t &parts_,
      uint64_t *message_id_inout_);
    static bool enqueue_mesh_broadcast_pending (
      spot_data_plane_runtime_state_t *state_,
      const std::string &topic_,
      const spot_owned_msg_parts_t &parts_,
      uint64_t *message_id_inout_);
    static bool stage_publish_message (
      std::deque<spot_data_plane_runtime_state_t::staged_publish_entry_t> *queue_,
      const std::string &topic_,
      const spot_owned_msg_parts_t &parts_,
      bool need_local_,
      bool need_mesh_);
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
      socket_base_t *peer_ctrl_pub_,
      spot_runtime_t *runtime_,
      spot_data_plane_runtime_state_t *runtime_state_,
      spot_node_t *node_,
      spot_data_plane_protocol_state_t *state_);
    static int handle_ctrl_command (
      socket_base_t *ctrl_,
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
                                     const spot_owned_msg_parts_t &parts_);
    static int forward_mesh_pub (spot_runtime_t *runtime_,
                                 spot_data_plane_runtime_state_t *state_,
                                 const std::string &topic_,
                                 const spot_owned_msg_parts_t &parts_);
    static int stage_message (spot_data_plane_runtime_state_t *state_,
                              const std::string &topic_,
                              const spot_owned_msg_parts_t &parts_,
                              bool source_mesh_,
                              bool need_local_,
                              bool need_mesh_);
    static int flush_local_fanout_pending (spot_runtime_t *runtime_,
                                           spot_data_plane_runtime_state_t *state_,
                                           socket_base_t *relay_socket_ = NULL);
    static int flush_mesh_pub_pending (spot_runtime_t *runtime_,
                                       spot_data_plane_runtime_state_t *state_,
                                       socket_base_t *sender_socket_ = NULL);
    static int flush_staged_messages (spot_runtime_t *runtime_,
                                      spot_data_plane_runtime_state_t *state_);
    static int recv_and_forward_ingress (socket_base_t *src_,
                                         socket_base_t *mesh_pub_,
                                         socket_base_t *fanout_,
                                         spot_runtime_t *runtime_,
                                         spot_data_plane_runtime_state_t *state_,
                                         const spot_node_t *node_);
};
}

#endif
