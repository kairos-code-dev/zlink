/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_data_plane.hpp"
#include "services/spot/spot_auto_hwm_internal.hpp"
#include "services/spot/spot_runtime_internal.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_runtime.hpp"
#include "services/control/service_control_runtime.hpp"

#include "core/recv_internal.hpp"
#include "sockets/socket_base.hpp"
#include "utils/sleep.hpp"
#include "utils/random.hpp"

namespace zlink
{
namespace
{
static const int spot_runtime_default_close_timeout_ms = 2000;

static uint32_t resolve_spot_data_runtime_interval_ms ()
{
    const char *raw = std::getenv ("ZLINK_SPOT_DATA_RUNTIME_INTERVAL_MS");
    if (!raw || !*raw)
        return 1;

    char *end = NULL;
    const unsigned long parsed = std::strtoul (raw, &end, 10);
    if (!end || *end != '\0' || parsed == 0)
        return 1;
    return static_cast<uint32_t> (parsed);
}
}

void preserve_first_error (int rc_, int *first_error_)
{
    if (rc_ == 0 || !first_error_ || *first_error_ != 0)
        return;
    *first_error_ = errno != 0 ? errno : EIO;
}

int send_ascii_frame (socket_base_t *socket_,
                            const std::string &value_,
                            int flags_)
{
    msg_t msg;
    if (msg.init_size (value_.size ()) != 0)
        return -1;
    if (!value_.empty ())
        memcpy (msg.data (), value_.data (), value_.size ());
    const int rc = socket_->send (&msg, flags_);
    msg.close ();
    return rc;
}

void close_socket_ptr (socket_base_t **socket_p_)
{
    if (socket_p_ && *socket_p_) {
        socket_base_t *socket = *socket_p_;
        socket->stop ();
        socket->close ();
        *socket_p_ = NULL;
    }
}

bool spot_shutdown_debug_enabled ()
{
    return std::getenv ("ZLINK_DEBUG_SPOT_SHUTDOWN") != NULL;
}

size_t fill_runtime_socket_slot_refs (spot_runtime_t *runtime_,
                                      runtime_socket_slot_ref_t *out_)
{
    if (!runtime_ || !out_)
        return 0;

    size_t count = 0;
    out_[count++] =
      runtime_socket_slot_ref_t{&runtime_->data_ctrl_front,
                                &runtime_->data_ctrl_endpoint, false};
    out_[count++] =
      runtime_socket_slot_ref_t{&runtime_->data_ctrl_back,
                                &runtime_->data_ctrl_endpoint, false};
    out_[count++] =
      runtime_socket_slot_ref_t{&runtime_->mesh_pub, NULL, false};
    out_[count++] =
      runtime_socket_slot_ref_t{&runtime_->mesh_xsub, NULL, false};
    out_[count++] =
      runtime_socket_slot_ref_t{&runtime_->peer_ctrl_pub,
                                &runtime_->peer_ctrl_endpoint, false};
    out_[count++] =
      runtime_socket_slot_ref_t{&runtime_->peer_ctrl_sub, NULL, false};
    out_[count++] =
      runtime_socket_slot_ref_t{&runtime_->external_router, NULL, false};
    out_[count++] =
      runtime_socket_slot_ref_t{&runtime_->internal_router,
                                &runtime_->internal_router_endpoint, false};
    out_[count++] =
      runtime_socket_slot_ref_t{&runtime_->internal_router_tx,
                                &runtime_->internal_router_sender_endpoint, true};
    out_[count++] =
      runtime_socket_slot_ref_t{&runtime_->local_pub_ingress_sub,
                                &runtime_->pub_ingress_endpoint, false};
    out_[count++] =
      runtime_socket_slot_ref_t{&runtime_->local_fanout_xpub,
                                &runtime_->sub_fanout_endpoint, false};
    return count;
}

spot_runtime_t::spot_runtime_t (spot_node_t *owner_) :
    owner (owner_),
    data_ctrl_front (NULL),
    data_ctrl_back (NULL),
    mesh_pub (NULL),
    mesh_xsub (NULL),
    peer_ctrl_pub (NULL),
    peer_ctrl_sub (NULL),
    external_router (NULL),
    internal_router (NULL),
    internal_router_tx (NULL),
    local_pub_ingress_sub (NULL),
    local_fanout_xpub (NULL),
    data_plane_runtime (NULL),
    stop (0),
    node_id (generate_random ()),
    faulted (false),
    fault_errno (0),
    abortive_shutdown (false),
    shutdown_phase_value (spot_shutdown_phase_running),
    next_attachment_id (0),
    attachment_version (0)
{
    if (node_id == 0)
        node_id = 1;

    char buf[128];
    snprintf (buf, sizeof (buf), "inproc://zlink.spot.%u.pub-in", node_id);
    pub_ingress_endpoint = buf;
    snprintf (buf, sizeof (buf), "inproc://zlink.spot.%u.sub-out", node_id);
    sub_fanout_endpoint = buf;
    snprintf (buf, sizeof (buf), "inproc://zlink.spot.%u.node-router", node_id);
    internal_router_endpoint = buf;
    snprintf (buf, sizeof (buf), "inproc://zlink.spot.%u.ctrl", node_id);
    data_ctrl_endpoint = buf;
}

spot_node_hwm_config_t spot_runtime_t::hwm_config_snapshot () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (hwm_config_sync));
    return hwm_config;
}

uint64_t spot_runtime_t::attachment_state_version () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (attachment_sync));
    return attachment_version;
}

ctx_t *spot_runtime_t::ctx () const
{
    return owner ? owner->_ctx : NULL;
}

void spot_runtime_t::set_hwm_config (const spot_node_hwm_config_t &config_)
{
    scoped_lock_t lock (hwm_config_sync);
    hwm_config = config_;
}

void spot_runtime_t::set_external_route_id (
  const std::string &peer_endpoint_,
  const std::string &route_id_)
{
    if (peer_endpoint_.empty ())
        return;

    scoped_lock_t lock (routed_send_sync);
    if (route_id_.empty ())
        external_route_ids_by_endpoint.erase (peer_endpoint_);
    else
        external_route_ids_by_endpoint[peer_endpoint_] = route_id_;
}

void spot_runtime_t::erase_external_route_id (
  const std::string &peer_endpoint_)
{
    scoped_lock_t lock (routed_send_sync);
    external_route_ids_by_endpoint.erase (peer_endpoint_);
}

std::vector<std::string> spot_runtime_t::clear_external_route_ids ()
{
    std::vector<std::string> peer_endpoints;
    scoped_lock_t lock (routed_send_sync);
    for (std::map<std::string, std::string>::const_iterator it =
           external_route_ids_by_endpoint.begin ();
         it != external_route_ids_by_endpoint.end (); ++it)
        peer_endpoints.push_back (it->first);
    external_route_ids_by_endpoint.clear ();
    return peer_endpoints;
}

std::vector<std::string> spot_runtime_t::external_route_ids_for_destination (
  const std::string &destination_node_rid_) const
{
    std::vector<std::string> route_ids;
    scoped_lock_t lock (const_cast<mutex_t &> (routed_send_sync));
    for (std::map<std::string, std::string>::const_iterator it =
           external_route_ids_by_endpoint.begin ();
         it != external_route_ids_by_endpoint.end (); ++it) {
        if (it->second == destination_node_rid_) {
            route_ids.push_back (it->second);
            return route_ids;
        }
    }
    if (external_route_ids_by_endpoint.size () == 1)
        route_ids.push_back (external_route_ids_by_endpoint.begin ()->second);
    return route_ids;
}

int spot_runtime_t::start ()
{
    if (!owner || !owner->_ctx) {
        errno = EFAULT;
        return -1;
    }

    data_ctrl_front = owner->_ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    if (!data_ctrl_front
        || data_ctrl_front->bind (data_ctrl_endpoint.c_str ()) != 0) {
        close_socket_ptr (&data_ctrl_front);
        return -1;
    }
    data_ctrl_front->set_auto_hwm_policy_enabled (false);
    size_t local_pub_count = 0;
    size_t local_sub_count = 0;
    size_t connected_peer_count = 0;
    size_t active_peer_count = 0;
    snapshot_auto_hwm_inputs (&local_pub_count, &local_sub_count,
                              &connected_peer_count, &active_peer_count);
    apply_spot_internal_auto_hwm (
      owner->_ctx, data_ctrl_front,
      spot_internal_auto_hwm_policy_t{auto_hwm_role_control,
                                      ZLINK_CORE_SOCKET_PAIR,
                                      connected_peer_count,
                                      active_peer_count,
                                      0, 0, true, true, true, true});
    owner->track_owned_socket (data_ctrl_front);

    const int linger = 0;
    const int timeout = 2000;
    data_ctrl_front->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger,
                                 sizeof (linger));
    data_ctrl_front->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &timeout,
                                 sizeof (timeout));
    data_ctrl_front->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &timeout,
                                 sizeof (timeout));

    int worker_errno = 0;
    if (spot_data_plane_t::initialize_runtime (owner, this,
                                               &execution.data_plane_state)
        != 0
        || !spot_node_t::recv_ctrl_reply (data_ctrl_front, &worker_errno)) {
        errno = worker_errno != 0 ? worker_errno : ETIMEDOUT;
        stop.set (1);
        stop_sockets ();
        spot_data_plane_t::teardown_runtime (
          owner, this, &execution.data_plane_state,
          &execution.data_plane_protocol_state);
        if (owner)
            owner->untrack_owned_socket (data_ctrl_front);
        close_socket_ptr (&data_ctrl_front);
        return -1;
    }

    data_plane_runtime = owner->_ctx->service_data_runtime_for_key (node_id);
    if (!data_plane_runtime) {
        errno = ETERM;
        stop.set (1);
        stop_sockets ();
        spot_data_plane_t::teardown_runtime (
          owner, this, &execution.data_plane_state,
          &execution.data_plane_protocol_state);
        return -1;
    }

    const uint64_t task_id =
      data_plane_runtime->add_periodic_task (&spot_data_plane_t::task_entry,
                                             owner,
                                             resolve_spot_data_runtime_interval_ms (),
                                             true);
    if (task_id == 0) {
        stop.set (1);
        stop_sockets ();
        spot_data_plane_t::teardown_runtime (
          owner, this, &execution.data_plane_state,
          &execution.data_plane_protocol_state);
        return -1;
    }
    execution.data_plane_task_id_value = task_id;
    execution.data_plane_running = true;
    return 0;
}

int spot_runtime_t::ensure_healthy () const
{
    if (stop.get () != 0 || faulted) {
        errno = EFSM;
        return -1;
    }
    return 0;
}


int spot_runtime_t::send_command (const char *verb_, const char *arg_) const
{
    if (!data_ctrl_front) {
        errno = EFAULT;
        return -1;
    }

    scoped_lock_t lock (const_cast<mutex_t &> (ctrl_sync));
    if (send_ascii_frame (data_ctrl_front, verb_,
                                arg_ ? ZLINK_SNDMORE : 0)
        != 0)
        return -1;
    if (arg_ && send_ascii_frame (data_ctrl_front, arg_, 0) != 0)
        return -1;

    if (data_plane_runtime && execution.data_plane_task_id_value != 0)
        data_plane_runtime->wakeup_task (execution.data_plane_task_id_value);

    int reply_errno = 0;
    if (!spot_node_t::recv_ctrl_reply (data_ctrl_front, &reply_errno)) {
        errno = reply_errno != 0 ? reply_errno : ETIMEDOUT;
        return -1;
    }
    return 0;
}

void spot_runtime_t::mark_fault (int err_)
{
    faulted = true;
    fault_errno = err_ != 0 ? err_ : EIO;
}

void spot_runtime_t::begin_shutdown ()
{
    stop.set (1);
    advance_shutdown_phase (spot_shutdown_phase_stop_accepting);
}

void spot_runtime_t::advance_shutdown_phase (spot_shutdown_phase_t phase_)
{
    scoped_lock_t lock (shutdown_sync);
    if (phase_ > shutdown_phase_value)
        shutdown_phase_value = phase_;
}

spot_shutdown_phase_t spot_runtime_t::shutdown_phase () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (shutdown_sync));
    return shutdown_phase_value;
}


}
