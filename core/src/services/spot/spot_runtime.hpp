/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_RUNTIME_HPP_INCLUDED__
#define __ZLINK_SPOT_RUNTIME_HPP_INCLUDED__

#include "services/spot/spot_runtime_execution.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/mutex.hpp"
#include "utils/stdint.hpp"

#include <map>
#include <string>

namespace zlink
{
class socket_base_t;
class spot_node_t;
class service_control_runtime_t;

enum spot_attachment_kind_t
{
    spot_attachment_pub = 1,
    spot_attachment_sub = 2
};

enum spot_runtime_sender_kind_t
{
    spot_runtime_sender_route_ingress = 1,
    spot_runtime_sender_node_router = 2
};

enum spot_shutdown_phase_t
{
    spot_shutdown_phase_running = 0,
    spot_shutdown_phase_stop_accepting = 1,
    spot_shutdown_phase_stop_producers = 2,
    spot_shutdown_phase_detach_endpoints = 3,
    spot_shutdown_phase_close_transports = 4,
    spot_shutdown_phase_drain_state = 5,
    spot_shutdown_phase_cleanup = 6
};

struct spot_attachment_t
{
    spot_attachment_t () :
        id (0),
        kind (0),
        socket (NULL)
    {
    }

    uint64_t id;
    int kind;
    socket_base_t *socket;
    std::string endpoint;
};

struct spot_node_hwm_config_t
{
    spot_node_hwm_config_t () :
        topic_send_enabled (false),
        topic_send_hwm (0),
        topic_recv_enabled (false),
        topic_recv_hwm (0),
        routed_send_enabled (false),
        routed_send_hwm (0),
        routed_recv_enabled (false),
        routed_recv_hwm (0)
    {
    }

    bool topic_send_enabled;
    int topic_send_hwm;
    bool topic_recv_enabled;
    int topic_recv_hwm;
    bool routed_send_enabled;
    int routed_send_hwm;
    bool routed_recv_enabled;
    int routed_recv_hwm;
};

struct spot_runtime_t
{
    explicit spot_runtime_t (spot_node_t *owner_);

    int start ();
    int create_attachment (int kind_,
                           const char *endpoint_,
                           uint64_t *out_id_);
    socket_base_t *attachment_socket (uint64_t id_) const;
    int destroy_attachment (uint64_t id_);
    int destroy_attachment_async (uint64_t id_);
    int ensure_healthy () const;
    void stop_sockets ();
    int close_control_sockets ();
    int close_runtime_socket (socket_base_t *&socket_, int timeout_ms_);
    int close_runtime_socket_async (socket_base_t *&socket_, int timeout_ms_);
    int send_command (const char *verb_, const char *arg_) const;
    void mark_fault (int err_);
    int ensure_sender_socket (spot_runtime_sender_kind_t kind_,
                              socket_base_t **out_);
    int ensure_peer_route_sender_socket (const std::string &target_endpoint_,
                                         socket_base_t **out_);
    int close_sender_cache (spot_runtime_sender_kind_t kind_, int timeout_ms_);
    int close_sender_caches (int timeout_ms_);
    void begin_shutdown ();
    void advance_shutdown_phase (spot_shutdown_phase_t phase_);
    spot_shutdown_phase_t shutdown_phase () const;
    int detach_runtime_endpoints ();
    bool try_set_data_plane_task_id (uint64_t task_id_);
    uint64_t data_plane_task_id () const;
    uint64_t clear_data_plane_task_id ();
    bool try_set_control_task_id (uint64_t task_id_);
    uint64_t control_task_id () const;
    uint64_t clear_control_task_id ();
    bool note_connected_peer_version (uint64_t connected_peer_version_);
    uint64_t connected_peer_version_seen () const;
    int stop_and_join ();
    int abortive_stop ();
    size_t live_socket_slot_count () const;
    size_t attachment_count () const;
    spot_node_hwm_config_t hwm_config_snapshot () const;
    void set_hwm_config (const spot_node_hwm_config_t &config_);

    spot_node_t *owner;
    mutable mutex_t ctrl_sync;
    mutable mutex_t execution_sync;
    mutable mutex_t hwm_config_sync;
    mutable mutex_t routed_send_sync;
    mutable mutex_t shutdown_sync;
    socket_base_t *data_ctrl_front;
    socket_base_t *data_ctrl_back;
    socket_base_t *mesh_pub;
    socket_base_t *mesh_xsub;
    socket_base_t *peer_ctrl_pub;
    socket_base_t *peer_ctrl_sub;
    socket_base_t *route_ingress;
    socket_base_t *peer_route_ingress;
    socket_base_t *node_router;
    socket_base_t *route_ingress_tx;
    socket_base_t *node_router_tx;
    socket_base_t *peer_route_tx;
    socket_base_t *local_pub_ingress_sub;
    socket_base_t *local_fanout_xpub;
    service_control_runtime_t *data_plane_runtime;
    atomic_counter_t stop;
    uint32_t node_id;
    std::string bound_endpoint;
    std::string pub_ingress_endpoint;
    std::string sub_fanout_endpoint;
    std::string route_ingress_endpoint;
    std::string node_router_endpoint;
    std::string data_ctrl_endpoint;
    std::string peer_ctrl_endpoint;
    std::string peer_route_bind_endpoint;
    std::string route_ingress_sender_endpoint;
    std::string node_router_sender_endpoint;
    std::string peer_route_sender_endpoint;
    uint64_t peer_route_sender_ready_after_ms;
    std::string routed_mesh_subscription_topic;
    bool faulted;
    int fault_errno;
    bool abortive_shutdown;
    spot_shutdown_phase_t shutdown_phase_value;
    spot_node_hwm_config_t hwm_config;
    mutable mutex_t attachment_sync;
    spot_runtime_execution_state_t execution;
    uint64_t next_attachment_id;
    std::map<uint64_t, spot_attachment_t> attachments;
};
}

#endif
