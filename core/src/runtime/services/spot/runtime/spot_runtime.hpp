/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_RUNTIME_HPP_INCLUDED__
#define __ZLINK_SPOT_RUNTIME_HPP_INCLUDED__

#include "services/spot/runtime/spot_runtime_attachment_state.hpp"
#include "services/spot/runtime/spot_runtime_execution.hpp"
#include "services/spot/runtime/spot_runtime_hwm.hpp"
#include "services/spot/runtime/spot_runtime_routes.hpp"
#include "services/spot/dispatch/spot_dispatch_worker_pool.hpp"
#include "core/thread.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/mutex.hpp"
#include "utils/stdint.hpp"

#include <string>
#include <vector>

namespace zlink
{
class socket_base_t;
class spot_node_t;
class ctx_t;

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

struct spot_runtime_t
{
    explicit spot_runtime_t (spot_node_t *owner_);

    ctx_t *ctx () const;

    int start ();
    int create_attachment (int kind_, const char *endpoint_, uint64_t *out_id_);
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
    void begin_shutdown ();
    void advance_shutdown_phase (spot_shutdown_phase_t phase_);
    spot_shutdown_phase_t shutdown_phase () const;
    int detach_runtime_endpoints ();
    bool try_set_control_task_id (uint64_t task_id_);
    uint64_t control_task_id () const;
    uint64_t clear_control_task_id ();
    bool note_connected_peer_version (uint64_t connected_peer_version_);
    uint64_t connected_peer_version_seen () const;
    int stop_and_join ();
    int abortive_stop ();
    size_t live_socket_slot_count () const;
    size_t attachment_count () const;
    size_t attachment_count_by_kind (int kind_) const;
    void snapshot_auto_hwm_inputs (size_t *local_pub_count_out_,
                                   size_t *local_sub_count_out_,
                                   size_t *connected_peer_count_out_,
                                   size_t *active_peer_count_out_) const;
    void refresh_attachment_auto_hwm_scope_counts ();
    uint64_t attachment_state_version () const;
    spot_node_runtime_tuning_t runtime_tuning_snapshot () const;
    void set_runtime_tuning (const spot_node_runtime_tuning_t &config_);
    int start_dispatch_workers ();
    void stop_dispatch_workers ();
    int post_dispatch_event (void *spot_);
    int post_dispatch_task (spot_dispatch_worker_pool_t::task_fn_t fn_, void *arg_);
    void set_external_route_id (const std::string &peer_endpoint_, const std::string &route_id_);
    void set_external_route_id (const std::string &peer_endpoint_,
                                const std::string &route_id_,
                                const std::string &route_endpoint_);
    bool external_route_id_matches (const std::string &peer_endpoint_,
                                    const std::string &route_id_,
                                    const std::string &route_endpoint_) const;
    std::string erase_external_route_id (const std::string &peer_endpoint_);
    std::vector<std::string> clear_external_route_ids ();
    bool missing_external_routes_for_ready_peer () const;
    std::vector<std::string>
    external_route_ids_for_destination (const std::string &destination_node_rid_) const;

    spot_node_t *owner;
    mutable mutex_t ctrl_sync;
    mutable mutex_t execution_sync;
    mutable mutex_t runtime_tuning_sync;
    mutable mutex_t shutdown_sync;
    socket_base_t *data_ctrl_front;
    socket_base_t *data_ctrl_back;
    socket_base_t *mesh_pub;
    socket_base_t *mesh_xsub;
    socket_base_t *pub_ingress_sub;
    socket_base_t *peer_ctrl_pub;
    socket_base_t *peer_ctrl_sub;
    socket_base_t *external_router;
    socket_base_t *local_fanout_xpub;
    thread_t data_plane_thread;
    spot_dispatch_worker_pool_t *dispatch_workers;
    atomic_counter_t stop;
    uint32_t node_id;
    std::string bound_endpoint;
    std::string sub_fanout_endpoint;
    std::string pub_ingress_endpoint;
    std::string data_ctrl_endpoint;
    std::string peer_ctrl_endpoint;
    std::string external_router_bind_endpoint;
    spot_runtime_external_routes_t external_routes;
    bool faulted;
    int fault_errno;
    bool abortive_shutdown;
    spot_shutdown_phase_t shutdown_phase_value;
    spot_node_runtime_tuning_t runtime_tuning;
    spot_runtime_execution_state_t execution;
    spot_runtime_attachment_state_t attachment_state;
};
}

#endif
