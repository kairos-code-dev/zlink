/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SPOT_DATA_PLANE_RUNTIME_STATE_HPP_INCLUDED
#define ZLINK_SPOT_DATA_PLANE_RUNTIME_STATE_HPP_INCLUDED

#include <zlink.h>

#include "core/signaler.hpp"
#include "core/socket_poller.hpp"
#include "services/spot/common/spot_message_parts_internal.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace zlink
{
class socket_base_t;
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

    struct mesh_pub_hwm_state_t
    {
        mesh_pub_hwm_state_t () :
            current_sndhwm (0),
            last_hwm_version (UINT64_MAX)
        {
        }

        int current_sndhwm;
        uint64_t last_hwm_version;
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
            pending_hard_limit (0)
        {
        }

        size_t pending_bytes;
        size_t pending_pause_threshold;
        size_t pending_resume_threshold;
        size_t pending_hard_limit;
        pending_message_map_t pending_messages;
        target_map_t targets;
        std::unordered_map<socket_base_t *, uint64_t> target_by_socket;
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

    struct publish_ingress_queue_t
    {
        publish_ingress_queue_t () :
            queued_bytes (0),
            backpressure_active (false),
            signal_armed (false),
            closed (false)
        {
        }

        std::mutex mutex;
        std::condition_variable cv;
        std::deque<staged_publish_entry_t> messages;
        size_t queued_bytes;
        bool backpressure_active;
        signaler_t signaler;
        bool signal_armed;
        bool closed;
    };

    struct routed_send_entry_t
    {
        std::vector<zlink_msg_t> parts;
        zlink_send_flags_t flags;
    };

    struct external_router_ingress_entry_t
    {
        std::vector<zlink_msg_t> parts;
    };

    struct routed_send_queue_t
    {
        routed_send_queue_t () :
            queued_bytes (0),
            retry_after_ms (0),
            backpressure_active (false),
            signal_armed (false),
            closed (false)
        {
        }

        std::mutex mutex;
        std::condition_variable cv;
        std::deque<routed_send_entry_t> messages;
        size_t queued_bytes;
        uint64_t retry_after_ms;
        bool backpressure_active;
        signaler_t signaler;
        bool signal_armed;
        bool closed;
    };

    struct external_router_ingress_queue_t
    {
        external_router_ingress_queue_t () :
            signal_armed (false),
            closed (false)
        {
        }

        std::mutex mutex;
        std::deque<external_router_ingress_entry_t> messages;
        signaler_t signaler;
        bool signal_armed;
        bool closed;
    };

    struct poller_interest_state_t
    {
        poller_interest_state_t () :
            mesh_xsub_pollin_paused (false),
            mesh_xsub_pollin_armed (true)
        {
        }

        bool mesh_xsub_pollin_paused;
        bool mesh_xsub_pollin_armed;
    };

    struct mesh_peer_observer_state_t
    {
        mesh_peer_observer_state_t () :
            pub_monitor (NULL),
            xsub_monitor (NULL)
        {
        }

        bool owns (const socket_base_t *socket_) const
        {
            return socket_ == pub_monitor || socket_ == xsub_monitor;
        }

        socket_base_t *pub_monitor;
        socket_base_t *xsub_monitor;
    };

    socket_base_t *ctrl;
    socket_base_t *mesh_pub;
    socket_base_t *mesh_xsub;
    socket_base_t *pub_ingress_sub;
    mesh_peer_observer_state_t mesh_peer_observer;
    socket_base_t *peer_ctrl_pub;
    socket_base_t *peer_ctrl_sub;
    socket_base_t *external_router;
    socket_base_t *fanout;
    uint64_t next_pending_message_id;
    uint64_t last_attachment_version;
    bool runtime_sockets_nodelay_applied;
    mesh_pub_hwm_state_t mesh_pub_hwm;
    poller_interest_state_t interest;
    publish_ingress_queue_t publish_ingress;
    routed_send_queue_t routed_send;
    external_router_ingress_queue_t external_router_ingress;
    local_fanout_state_t local_fanout;
    remote_mesh_state_t remote_mesh;
    staged_publish_state_t staged;
    socket_poller_t *poller;
    std::vector<socket_poller_t::event_t> poll_events;
};

}

#endif
