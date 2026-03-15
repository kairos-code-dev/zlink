/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_RUNTIME_HPP_INCLUDED__
#define __ZLINK_SPOT_RUNTIME_HPP_INCLUDED__

#include "core/thread.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/mutex.hpp"
#include "utils/stdint.hpp"

#include <atomic>
#include <map>
#include <set>
#include <string>

namespace zlink
{
class socket_base_t;
class spot_node_t;

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

struct spot_runtime_t
{
    explicit spot_runtime_t (spot_node_t *owner_);

    int start ();
    int create_attachment (int kind_,
                           const char *endpoint_,
                           uint64_t *out_id_);
    socket_base_t *attachment_socket (uint64_t id_) const;
    int destroy_attachment (uint64_t id_);
    int ensure_healthy () const;
    void stop_sockets ();
    int close_control_sockets ();
    int send_command (const char *verb_, const char *arg_) const;
    void mark_fault (int err_);
    int stop_and_join ();
    int abortive_stop ();
    size_t live_socket_slot_count () const;
    size_t attachment_count () const;

    spot_node_t *owner;
    mutable mutex_t ctrl_sync;
    socket_base_t *data_ctrl_front;
    socket_base_t *data_ctrl_back;
    socket_base_t *mesh_pub;
    socket_base_t *mesh_xsub;
    socket_base_t *peer_ctrl_pub;
    socket_base_t *peer_ctrl_sub;
    socket_base_t *local_pub_ingress_sub;
    socket_base_t *local_fanout_xpub;
    thread_t data_plane_thread;
    atomic_counter_t stop;
    uint64_t task_id;
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
    mutable mutex_t connected_peer_sync;
    std::atomic<uint64_t> connected_peer_version;
    uint64_t next_attachment_id;
    std::map<uint64_t, spot_attachment_t> attachments;
    std::set<std::string> connected_peer_endpoints;
};
}

#endif
