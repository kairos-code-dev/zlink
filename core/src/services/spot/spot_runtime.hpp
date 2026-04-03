/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_RUNTIME_HPP_INCLUDED__
#define __ZLINK_SPOT_RUNTIME_HPP_INCLUDED__

#include "services/spot/spot_data_plane_internal.hpp"
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

struct spot_control_runtime_state_t
{
    spot_control_runtime_state_t () :
        task_id (0),
        connected_peer_version_seen (0)
    {
    }

    uint64_t task_id;
    uint64_t connected_peer_version_seen;
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

    spot_node_t *owner;
    mutable mutex_t ctrl_sync;
    mutable mutex_t control_state_sync;
    socket_base_t *data_ctrl_front;
    socket_base_t *data_ctrl_back;
    socket_base_t *mesh_pub;
    socket_base_t *mesh_xsub;
    socket_base_t *peer_ctrl_pub;
    socket_base_t *peer_ctrl_sub;
    socket_base_t *local_pub_ingress_sub;
    socket_base_t *local_fanout_xpub;
    service_control_runtime_t *data_plane_runtime;
    atomic_counter_t stop;
    uint32_t node_id;
    std::string bound_endpoint;
    std::string pub_ingress_endpoint;
    std::string sub_fanout_endpoint;
    std::string data_ctrl_endpoint;
    std::string peer_ctrl_endpoint;
    bool faulted;
    int fault_errno;
    bool abortive_shutdown;
    mutable mutex_t attachment_sync;
    uint64_t data_plane_task_id_value;
    bool data_plane_running;
    uint64_t next_bootstrap_ms;
    uint64_t last_bootstrap_peer_version;
    spot_data_plane_runtime_state_t data_plane_state;
    spot_data_plane_protocol_state_t data_plane_protocol_state;
    spot_control_runtime_state_t control_state;
    spot_mesh_peer_state_t mesh_peer_state;
    uint64_t next_attachment_id;
    std::map<uint64_t, spot_attachment_t> attachments;
};
}

#endif
