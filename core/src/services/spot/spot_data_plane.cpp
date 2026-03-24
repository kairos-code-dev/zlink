/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_control_protocol.hpp"
#include "services/spot/spot_data_plane.hpp"
#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_runtime.hpp"

#include "services/common/monitor_decode.hpp"
#include "services/common/socket_monitor_bridge.hpp"
#include "core/socket_poller.hpp"
#include "sockets/socket_base.hpp"
#include "utils/clock.hpp"

#include <errno.h>
#include <string.h>

namespace zlink
{
namespace
{
static const size_t spot_sub_queue_hwm_default = 64;
static const int spot_internal_ingress_rcvhwm_default = 8192;
static const int spot_internal_mesh_xsub_rcvhwm_default = 8192;
static const int spot_internal_peer_ctrl_rcvhwm_default = 1024;

static int apply_common_internal_opts (socket_base_t *socket_, int linger_)
{
    return socket_->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &linger_,
                                sizeof (linger_));
}

static void clear_runtime_socket_refs (spot_runtime_t *runtime_)
{
    if (!runtime_)
        return;
    runtime_->data_ctrl_back = NULL;
    runtime_->mesh_pub = NULL;
    runtime_->mesh_xsub = NULL;
    runtime_->peer_ctrl_pub = NULL;
    runtime_->peer_ctrl_sub = NULL;
    runtime_->local_pub_ingress_sub = NULL;
    runtime_->local_fanout_xpub = NULL;
}

static void reset_mesh_pub_budget_state (spot_runtime_t *runtime_)
{
    if (!runtime_)
        return;

    runtime_->mesh_pub_ready_peer_count.store (0,
                                               std::memory_order_release);
    runtime_->mesh_pub_budget_version.fetch_add (1,
                                                 std::memory_order_acq_rel);
}

static int resolve_mesh_pub_sndhwm_default (const spot_runtime_t *runtime_)
{
    if (!runtime_)
        return zlink::resolve_mesh_pub_sndhwm_default (std::string (), 0);

    const uint32_t ready_peers =
      runtime_->mesh_pub_ready_peer_count.load (std::memory_order_acquire);
    return zlink::resolve_mesh_pub_sndhwm_default (runtime_->bound_endpoint,
                                                   ready_peers);
}

static void refresh_mesh_pub_sndhwm (spot_runtime_t *runtime_,
                                     socket_base_t *mesh_pub_,
                                     int *current_hwm_,
                                     uint64_t *last_budget_version_,
                                     std::string *last_bound_endpoint_)
{
    if (!runtime_ || !mesh_pub_ || !current_hwm_ || !last_budget_version_
        || !last_bound_endpoint_) {
        return;
    }

    const uint64_t budget_version =
      runtime_->mesh_pub_budget_version.load (std::memory_order_acquire);
    const std::string bound_endpoint = runtime_->bound_endpoint;
    if (budget_version == *last_budget_version_
        && bound_endpoint == *last_bound_endpoint_) {
        return;
    }

    *last_budget_version_ = budget_version;
    *last_bound_endpoint_ = bound_endpoint;

    const int desired = spot_data_plane_forwarder_t::resolve_internal_hwm_override (
      "ZLINK_SPOT_INTERNAL_MESH_PUB_SNDHWM",
      resolve_mesh_pub_sndhwm_default (runtime_));
    if (desired == *current_hwm_)
        return;

    if (mesh_pub_->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &desired,
                               sizeof (desired))
        == 0) {
        *current_hwm_ = desired;
    }
}
}

void spot_data_plane_t::close_socket_ptr (spot_node_t *node_,
                                          socket_base_t *&socket_)
{
    if (!socket_)
        return;
    if (!node_ || !node_->_ctx) {
        socket_->stop ();
        socket_->close ();
        socket_ = NULL;
        return;
    }
    (void) node_->_lifecycle.close_socket (socket_, 2000);
}

void spot_data_plane_t::thread_entry (void *arg_)
{
    run (static_cast<spot_node_t *> (arg_));
}

void spot_data_plane_t::run (spot_node_t *node_)
{
    if (!node_)
        return;
    spot_runtime_t *runtime = node_->_runtime;
    if (!runtime)
        return;

    socket_base_t *ctrl = node_->_ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    socket_base_t *mesh_pub = node_->_ctx->create_socket (ZLINK_CORE_SOCKET_PUB);
    socket_base_t *mesh_xsub = node_->_ctx->create_socket (ZLINK_CORE_SOCKET_XSUB);
    socket_base_t *mesh_xsub_monitor = NULL;
    socket_base_t *peer_ctrl_pub = node_->_ctx->create_socket (ZLINK_CORE_SOCKET_PUB);
    socket_base_t *peer_ctrl_sub = node_->_ctx->create_socket (ZLINK_CORE_SOCKET_SUB);
    socket_base_t *ingress = node_->_ctx->create_socket (ZLINK_CORE_SOCKET_SUB);
    socket_base_t *fanout = node_->_ctx->create_socket (ZLINK_CORE_SOCKET_PUB);

    if (!ctrl || !mesh_pub || !mesh_xsub || !peer_ctrl_pub || !peer_ctrl_sub
        || !ingress || !fanout) {
        const int err = errno != 0 ? errno : ENOMEM;
        if (ctrl && ctrl->connect (runtime->data_ctrl_endpoint.c_str ()) == 0)
            (void) spot_data_plane_protocol_t::send_errno_reply (ctrl, err);
        close_socket_ptr (node_, fanout);
        close_socket_ptr (node_, ingress);
        close_socket_ptr (node_, peer_ctrl_sub);
        close_socket_ptr (node_, peer_ctrl_pub);
        close_socket_ptr (node_, mesh_xsub_monitor);
        close_socket_ptr (node_, mesh_xsub);
        close_socket_ptr (node_, mesh_pub);
        close_socket_ptr (node_, ctrl);
        {
            scoped_lock_t lock (node_->_sync);
            clear_runtime_socket_refs (runtime);
            runtime->mark_fault (err);
        }
        spot_data_plane_protocol_t::clear_mesh_xsub_connected_endpoints (runtime);
        return;
    }

    {
        scoped_lock_t lock (node_->_sync);
        runtime->data_ctrl_back = ctrl;
        runtime->mesh_pub = mesh_pub;
        runtime->mesh_xsub = mesh_xsub;
        runtime->peer_ctrl_pub = peer_ctrl_pub;
        runtime->peer_ctrl_sub = peer_ctrl_sub;
        runtime->local_pub_ingress_sub = ingress;
        runtime->local_fanout_xpub = fanout;
        node_->track_owned_socket (ctrl);
        node_->track_owned_socket (mesh_pub);
        node_->track_owned_socket (mesh_xsub);
        node_->track_owned_socket (peer_ctrl_pub);
        node_->track_owned_socket (peer_ctrl_sub);
        node_->track_owned_socket (ingress);
        node_->track_owned_socket (fanout);
    }

    const int linger = 0;
    const int zero = 0;
    const int neg_one = -1;
    const int one = 1;
    const int ingress_rcvhwm =
      spot_data_plane_forwarder_t::resolve_internal_hwm_override (
        "ZLINK_SPOT_INTERNAL_INGRESS_RCVHWM",
        spot_internal_ingress_rcvhwm_default);
    const int mesh_xsub_rcvhwm =
      spot_data_plane_forwarder_t::resolve_internal_hwm_override (
        "ZLINK_SPOT_INTERNAL_MESH_XSUB_RCVHWM",
        spot_internal_mesh_xsub_rcvhwm_default);
    const int mesh_pub_sndhwm =
      spot_data_plane_forwarder_t::resolve_internal_hwm_override (
        "ZLINK_SPOT_INTERNAL_MESH_PUB_SNDHWM",
        zlink::resolve_mesh_pub_sndhwm_default (std::string (), 0));
    const int peer_ctrl_rcvhwm =
      spot_data_plane_forwarder_t::resolve_internal_hwm_override (
        "ZLINK_SPOT_INTERNAL_PEER_CTRL_RCVHWM",
        spot_internal_peer_ctrl_rcvhwm_default);
    const int fanout_sndhwm =
      spot_data_plane_forwarder_t::resolve_internal_hwm_override (
        "ZLINK_SPOT_INTERNAL_FANOUT_SNDHWM",
        static_cast<int> (spot_sub_queue_hwm_default));

    apply_common_internal_opts (ctrl, linger);
    apply_common_internal_opts (mesh_pub, linger);
    apply_common_internal_opts (mesh_xsub, linger);
    apply_common_internal_opts (peer_ctrl_pub, linger);
    apply_common_internal_opts (peer_ctrl_sub, linger);
    apply_common_internal_opts (ingress, linger);
    apply_common_internal_opts (fanout, linger);

    ctrl->connect (runtime->data_ctrl_endpoint.c_str ());
    ingress->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &ingress_rcvhwm,
                         sizeof (ingress_rcvhwm));
    ingress->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &neg_one,
                         sizeof (neg_one));
    ingress->setsockopt (ZLINK_INTERNAL_OPT_SUBSCRIBE, "", 0);
    fanout->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &fanout_sndhwm,
                        sizeof (fanout_sndhwm));
    fanout->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &neg_one,
                        sizeof (neg_one));
    fanout->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &zero, sizeof (zero));
    fanout->setsockopt (ZLINK_INTERNAL_OPT_XPUB_NODROP, &one, sizeof (one));
    mesh_pub->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &mesh_pub_sndhwm,
                          sizeof (mesh_pub_sndhwm));
    mesh_pub->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &neg_one,
                          sizeof (neg_one));
    mesh_xsub->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &mesh_xsub_rcvhwm,
                           sizeof (mesh_xsub_rcvhwm));
    mesh_xsub->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &neg_one,
                           sizeof (neg_one));
    peer_ctrl_pub->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &neg_one,
                               sizeof (neg_one));
    peer_ctrl_sub->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &peer_ctrl_rcvhwm,
                               sizeof (peer_ctrl_rcvhwm));
    peer_ctrl_sub->setsockopt (ZLINK_INTERNAL_OPT_SUBSCRIBE,
                               spot_control_protocol::ctrl_prefix,
                               strlen (spot_control_protocol::ctrl_prefix));

    mesh_xsub_monitor = static_cast<socket_base_t *> (open_socket_monitor_bridge (
      mesh_xsub, ZLINK_EVENT_CONNECTION_READY_CHANGED | ZLINK_EVENT_DISCONNECTED));
    if (!mesh_xsub_monitor) {
        const int err = errno != 0 ? errno : EIO;
        (void) spot_data_plane_protocol_t::send_errno_reply (ctrl, err);
        close_socket_ptr (node_, fanout);
        close_socket_ptr (node_, ingress);
        close_socket_ptr (node_, peer_ctrl_sub);
        close_socket_ptr (node_, peer_ctrl_pub);
        close_socket_ptr (node_, mesh_xsub_monitor);
        close_socket_ptr (node_, mesh_xsub);
        close_socket_ptr (node_, mesh_pub);
        close_socket_ptr (node_, ctrl);
        {
            scoped_lock_t lock (node_->_sync);
            clear_runtime_socket_refs (runtime);
            runtime->mark_fault (err);
        }
        spot_data_plane_protocol_t::clear_mesh_xsub_connected_endpoints (runtime);
        return;
    }

    if (spot_data_plane_protocol_t::send_subscription_update (mesh_xsub, "",
                                                              true)
          != 0
        || ingress->bind (runtime->pub_ingress_endpoint.c_str ()) != 0
        || fanout->bind (runtime->sub_fanout_endpoint.c_str ()) != 0) {
        const int err = errno != 0 ? errno : EIO;
        (void) spot_data_plane_protocol_t::send_errno_reply (ctrl, err);
        (void) mesh_xsub->monitor (NULL, 0, 3, ZLINK_CORE_SOCKET_PAIR);
        close_socket_ptr (node_, fanout);
        close_socket_ptr (node_, ingress);
        close_socket_ptr (node_, peer_ctrl_sub);
        close_socket_ptr (node_, peer_ctrl_pub);
        close_socket_ptr (node_, mesh_xsub_monitor);
        close_socket_ptr (node_, mesh_xsub);
        close_socket_ptr (node_, mesh_pub);
        close_socket_ptr (node_, ctrl);
        {
            scoped_lock_t lock (node_->_sync);
            clear_runtime_socket_refs (runtime);
            runtime->mark_fault (err);
        }
        spot_data_plane_protocol_t::clear_mesh_xsub_connected_endpoints (runtime);
        return;
    }

    if (spot_data_plane_protocol_t::send_ok_reply (ctrl) != 0) {
        const int err = errno != 0 ? errno : EIO;
        (void) mesh_xsub->monitor (NULL, 0, 3, ZLINK_CORE_SOCKET_PAIR);
        close_socket_ptr (node_, fanout);
        close_socket_ptr (node_, ingress);
        close_socket_ptr (node_, peer_ctrl_sub);
        close_socket_ptr (node_, peer_ctrl_pub);
        close_socket_ptr (node_, mesh_xsub_monitor);
        close_socket_ptr (node_, mesh_xsub);
        close_socket_ptr (node_, mesh_pub);
        close_socket_ptr (node_, ctrl);
        {
            scoped_lock_t lock (node_->_sync);
            clear_runtime_socket_refs (runtime);
            runtime->mark_fault (err);
        }
        spot_data_plane_protocol_t::clear_mesh_xsub_connected_endpoints (runtime);
        return;
    }

    socket_poller_t poller;
    poller.add (ctrl, NULL, ZLINK_POLLIN);
    poller.add (ingress, NULL, ZLINK_POLLIN);
    poller.add (mesh_xsub, NULL, ZLINK_POLLIN);
    poller.add (peer_ctrl_sub, NULL, ZLINK_POLLIN);
    poller.add (mesh_xsub_monitor, NULL, ZLINK_POLLIN);

    bool running = true;
    int fatal_errno = 0;
    uint64_t next_bootstrap_ms = 0;
    spot_data_plane_protocol_state_t protocol_state;
    int current_mesh_pub_sndhwm = mesh_pub_sndhwm;
    uint64_t last_mesh_pub_budget_version = UINT64_MAX;
    std::string last_mesh_pub_bound_endpoint;

    while (running) {
        spot_data_plane_forwarder_t::pump_socket_commands (mesh_pub);
        spot_data_plane_forwarder_t::pump_socket_commands (mesh_xsub);
        spot_data_plane_forwarder_t::pump_socket_commands (peer_ctrl_pub);
        spot_data_plane_forwarder_t::pump_socket_commands (peer_ctrl_sub);
        spot_data_plane_forwarder_t::pump_socket_commands (ingress);
        spot_data_plane_forwarder_t::pump_socket_commands (fanout);

        mesh_pub->set_all_pipes_nodelay ();
        peer_ctrl_pub->set_all_pipes_nodelay ();
        peer_ctrl_sub->set_all_pipes_nodelay ();
        ingress->set_all_pipes_nodelay ();
        fanout->set_all_pipes_nodelay ();
        refresh_mesh_pub_sndhwm (runtime, mesh_pub, &current_mesh_pub_sndhwm,
                                 &last_mesh_pub_budget_version,
                                 &last_mesh_pub_bound_endpoint);

        if (spot_data_plane_protocol_t::recv_and_process_ctrl_messages (
              peer_ctrl_sub, node_, &protocol_state)
            != 0) {
            fatal_errno = errno;
            break;
        }

        socket_poller_t::event_t events[5];
        const int rc = poller.wait (events, 5, 20);
        if (rc < 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            fatal_errno = errno;
            break;
        }

        for (int pass = 0; pass < 3 && running; ++pass) {
            for (int i = 0; i < rc; ++i) {
                if ((events[i].events & ZLINK_POLLIN) == 0)
                    continue;

                const bool is_ctrl_event =
                  events[i].socket == ctrl || events[i].socket == peer_ctrl_sub
                  || events[i].socket == mesh_xsub_monitor;
                const bool is_mesh_event = events[i].socket == mesh_xsub;
                const bool is_ingress_event = events[i].socket == ingress;

                if ((pass == 0 && !is_ctrl_event)
                    || (pass == 1 && !is_mesh_event)
                    || (pass == 2 && !is_ingress_event)) {
                    continue;
                }

                if (events[i].socket == ctrl) {
                    std::vector<std::string> frames;
                    if (spot_data_plane_protocol_t::recv_ascii_command (ctrl,
                                                                        &frames)
                        != 0
                        || spot_data_plane_protocol_t::handle_ctrl_command (
                             ctrl, node_, runtime, mesh_pub, mesh_xsub,
                             peer_ctrl_pub, peer_ctrl_sub, frames,
                             &protocol_state, &running)
                             != 0) {
                        fatal_errno = errno != 0 ? errno : EIO;
                        running = false;
                    }
                    if (!running)
                        break;
                    continue;
                }

                if (events[i].socket == peer_ctrl_sub) {
                    if (spot_data_plane_protocol_t::recv_and_process_ctrl_messages (
                          peer_ctrl_sub, node_, &protocol_state)
                        != 0) {
                        fatal_errno = errno;
                        running = false;
                        break;
                    }
                    continue;
                }

                if (events[i].socket == mesh_xsub_monitor) {
                    while (running) {
                        zlink_monitor_event_t raw;
                        if (recv_socket_monitor_event (mesh_xsub_monitor, &raw,
                                                       ZLINK_DONTWAIT)
                            != 0) {
                            if (errno == EAGAIN || errno == EINTR)
                                break;
                            fatal_errno = errno;
                            running = false;
                            break;
                        }

                        switch (raw.event) {
                            case ZLINK_EVENT_CONNECTION_READY_CHANGED:
                                spot_data_plane_protocol_t::
                                  sync_mesh_xsub_connected_endpoint (
                                    runtime, raw, true);
                                break;

                            case ZLINK_EVENT_DISCONNECTED:
                                spot_data_plane_protocol_t::
                                  sync_mesh_xsub_connected_endpoint (
                                    runtime, raw, false);
                                break;

                            default:
                                break;
                        }
                    }
                    if (!running)
                        break;
                    continue;
                }

                if (events[i].socket == mesh_xsub) {
                    if (spot_data_plane_protocol_t::recv_and_dispatch_mesh_xsub (
                          mesh_xsub, fanout, peer_ctrl_pub, node_,
                          &protocol_state)
                        != 0) {
                        fatal_errno = errno;
                        running = false;
                        break;
                    }
                    continue;
                }

                if (events[i].socket == ingress) {
                    if (spot_data_plane_forwarder_t::recv_and_forward_ingress (
                          ingress, mesh_pub, fanout, node_)
                        != 0) {
                        fatal_errno = errno;
                        running = false;
                        break;
                    }
                    continue;
                }
            }
        }

        if (!running)
            break;

        const uint64_t now_ms = clock_t ().now_ms ();
        if (now_ms >= next_bootstrap_ms) {
            if (spot_data_plane_protocol_t::publish_bootstrap_descriptor (
                  mesh_pub, node_, runtime)
                != 0) {
                fatal_errno = errno;
                running = false;
                break;
            }
            next_bootstrap_ms =
              now_ms
              + spot_data_plane_protocol_t::
                  resolve_bootstrap_broadcast_interval_ms (
                    runtime,
                    !protocol_state.peer_ready_filters.empty ());
        }
    }

    spot_data_plane_protocol_t::clear_snapshot_sources (node_, &protocol_state);
    protocol_state.outbound_ready_filters.clear ();
    for (std::map<std::string, std::string>::const_iterator it =
           protocol_state.peer_ctrl_endpoints.begin ();
         it != protocol_state.peer_ctrl_endpoints.end (); ++it) {
        if (!it->second.empty ())
            (void) peer_ctrl_pub->term_endpoint (it->second.c_str ());
        if (!it->first.empty ())
            (void) mesh_xsub->term_endpoint (it->first.c_str ());
    }
    protocol_state.peer_ctrl_endpoints.clear ();

    (void) mesh_xsub->monitor (NULL, 0, 3, ZLINK_CORE_SOCKET_PAIR);
    close_socket_ptr (node_, mesh_xsub_monitor);

    {
        scoped_lock_t lock (node_->_sync);
        runtime->peer_ctrl_endpoint.clear ();
        runtime->bound_endpoint.clear ();
    }
    reset_mesh_pub_budget_state (runtime);
    spot_data_plane_protocol_t::clear_mesh_xsub_connected_endpoints (runtime);

    if (fatal_errno != 0 && runtime->stop.get () == 0) {
        scoped_lock_t lock (node_->_sync);
        runtime->mark_fault (fatal_errno);
    }
}
}
