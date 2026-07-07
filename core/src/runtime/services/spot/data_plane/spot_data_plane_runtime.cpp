/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/common/spot_control_protocol.hpp"
#include "services/spot/data_plane/spot_data_plane.hpp"
#include "services/spot/data_plane/spot_data_plane_protocol_internal.hpp"
#include "services/spot/data_plane/spot_data_plane_internal.hpp"
#include "services/spot/data_plane/spot_mesh_pub_hwm.hpp"
#include "services/spot/node/spot_node_access.hpp"
#include "services/spot/runtime/spot_runtime.hpp"

#include "api/spot/dispatch/service_spot_dispatch_surface_internal.hpp"
#include "api/socket/request_reply_protocol_internal.hpp"
#include "core/socket_poller.hpp"
#include "services/common/socket_monitor_bridge.hpp"
#include "sockets/common/socket_close_ops.hpp"
#include "sockets/common/socket_base.hpp"

#include <cstdlib>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace zlink
{
namespace
{
static void spot_data_plane_debug_logf (const char *fmt_, ...)
{
    if (!std::getenv ("ZLINK_DEBUG_SPOT_SHUTDOWN"))
        return;

    std::fprintf (stderr, "[spot-data-plane] ");
    va_list args;
    va_start (args, fmt_);
    std::vfprintf (stderr, fmt_, args);
    va_end (args);
    std::fflush (stderr);
}

static void close_mesh_peer_observer (spot_node_t *node_, spot_data_plane_runtime_state_t *state_)
{
    if (!state_)
        return;
    socket_base_t *pub_monitor = state_->mesh_peer_observer.pub_monitor;
    socket_base_t *xsub_monitor = state_->mesh_peer_observer.xsub_monitor;
    socket_base_t *pub_source_monitor = NULL;
    socket_base_t *xsub_source_monitor = NULL;
    state_->mesh_peer_observer.pub_monitor = NULL;
    state_->mesh_peer_observer.xsub_monitor = NULL;
    if (state_->mesh_pub) {
        spot_data_plane_debug_logf ("mesh_pub stop monitor begin sid=%d\n",
                                    state_->mesh_pub->socket_id ());
        pub_source_monitor = state_->mesh_pub->detach_monitor_socket (false);
        spot_data_plane_debug_logf ("mesh_pub stop monitor end\n");
    }
    if (state_->mesh_xsub) {
        spot_data_plane_debug_logf ("mesh_xsub stop monitor begin sid=%d\n",
                                    state_->mesh_xsub->socket_id ());
        xsub_source_monitor = state_->mesh_xsub->detach_monitor_socket (false);
        spot_data_plane_debug_logf ("mesh_xsub stop monitor end\n");
    }
    if (pub_monitor) {
        spot_data_plane_debug_logf ("close pub_monitor begin sid=%d\n", pub_monitor->socket_id ());
        spot_node_access_t::untrack_owned_socket (node_, pub_monitor);
        (void) socket_close_ops_t::request_close (pub_monitor, 0);
        spot_data_plane_debug_logf ("close pub_monitor end\n");
    }
    if (xsub_monitor) {
        spot_data_plane_debug_logf ("close xsub_monitor begin sid=%d\n",
                                    xsub_monitor->socket_id ());
        spot_node_access_t::untrack_owned_socket (node_, xsub_monitor);
        (void) socket_close_ops_t::request_close (xsub_monitor, 0);
        spot_data_plane_debug_logf ("close xsub_monitor end\n");
    }
    if (pub_source_monitor) {
        spot_data_plane_debug_logf ("close pub_source_monitor begin sid=%d\n",
                                    pub_source_monitor->socket_id ());
        (void) socket_close_ops_t::request_close (pub_source_monitor, 0);
        spot_data_plane_debug_logf ("close pub_source_monitor end\n");
    }
    if (xsub_source_monitor) {
        spot_data_plane_debug_logf ("close xsub_source_monitor begin sid=%d\n",
                                    xsub_source_monitor->socket_id ());
        (void) socket_close_ops_t::request_close (xsub_source_monitor, 0);
        spot_data_plane_debug_logf ("close xsub_source_monitor end\n");
    }
}

static int open_mesh_peer_observer (spot_data_plane_runtime_state_t *state_)
{
    if (!state_) {
        errno = EFAULT;
        return -1;
    }

    const int monitor_events = ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED;
    if (state_->mesh_pub) {
        state_->mesh_peer_observer.pub_monitor = static_cast<socket_base_t *> (
          open_socket_monitor_bridge (state_->mesh_pub, monitor_events));
        if (!state_->mesh_peer_observer.pub_monitor)
            return -1;
    }
    if (state_->mesh_xsub) {
        state_->mesh_peer_observer.xsub_monitor = static_cast<socket_base_t *> (
          open_socket_monitor_bridge (state_->mesh_xsub, monitor_events));
        if (!state_->mesh_peer_observer.xsub_monitor)
            return -1;
    }
    return 0;
}

static int add_mesh_peer_observer_to_poller (spot_data_plane_runtime_state_t *state_)
{
    if (!state_ || !state_->poller) {
        errno = EFAULT;
        return -1;
    }
    if (state_->mesh_peer_observer.xsub_monitor
        && state_->poller->add (state_->mesh_peer_observer.xsub_monitor, NULL, ZLINK_POLLIN) != 0)
        return -1;
    if (state_->mesh_peer_observer.pub_monitor
        && state_->poller->add (state_->mesh_peer_observer.pub_monitor, NULL, ZLINK_POLLIN) != 0)
        return -1;
    return 0;
}

static void close_runtime_sockets (spot_node_t *node_, spot_data_plane_runtime_state_t *state_)
{
    if (!state_)
        return;

    for (spot_data_plane_runtime_state_t::remote_mesh_state_t::target_map_t::iterator it =
           state_->remote_mesh.targets.begin ();
         it != state_->remote_mesh.targets.end (); ++it) {
        if (it->second.sender_socket && !it->second.route_endpoint.empty ())
            (void) it->second.sender_socket->term_endpoint (it->second.route_endpoint.c_str ());
        spot_data_plane_t::close_socket_ptr (node_, it->second.sender_socket);
    }
    state_->remote_mesh.targets.clear ();
    LIBZLINK_DELETE (state_->poller);
    state_->poller = NULL;
    spot_data_plane_t::close_socket_ptr (node_, state_->fanout);
    spot_data_plane_t::close_socket_ptr (node_, state_->routed_router);
    spot_data_plane_t::close_socket_ptr (node_, state_->peer_ctrl_sub);
    spot_data_plane_t::close_socket_ptr (node_, state_->peer_ctrl_pub);
    close_mesh_peer_observer (node_, state_);
    spot_data_plane_t::close_socket_ptr (node_, state_->pub_ingress_sub);
    spot_data_plane_t::close_socket_ptr (node_, state_->mesh_xsub);
    spot_data_plane_t::close_socket_ptr (node_, state_->mesh_pub);
    spot_data_plane_t::close_socket_ptr (node_, state_->ctrl);
}

static int subscribe_runtime_mesh_topics (spot_runtime_t *runtime_, socket_base_t *mesh_xsub_)
{
    if (!runtime_ || !mesh_xsub_ || !runtime_->owner) {
        errno = EFAULT;
        return -1;
    }

    if (spot_data_plane_protocol_t::send_subscription_update (
          mesh_xsub_, spot_control_protocol::bootstrap_ctrl_descriptor_topic, true)
        != 0) {
        return -1;
    }

    return 0;
}

}

spot_data_plane_runtime_state_t::spot_data_plane_runtime_state_t () :
    ctrl (NULL),
    mesh_pub (NULL),
    mesh_xsub (NULL),
    pub_ingress_sub (NULL),
    peer_ctrl_pub (NULL),
    peer_ctrl_sub (NULL),
    routed_router (NULL),
    fanout (NULL),
    next_pending_message_id (0),
    last_attachment_version (UINT64_MAX),
    runtime_sockets_nodelay_applied (false),
    poller (NULL)
{
}

void spot_data_plane_t::close_socket_ptr (spot_node_t *node_, socket_base_t *&socket_)
{
    if (!socket_)
        return;
    if (!spot_node_access_t::ctx (node_)) {
        socket_->stop ();
        socket_->close ();
        socket_ = NULL;
        return;
    }
    (void) spot_node_access_t::close_owned_socket (node_, socket_, 2000);
}

void spot_data_plane_t::clear_runtime_socket_refs (spot_runtime_t *runtime_)
{
    if (!runtime_)
        return;
    runtime_->data_ctrl_back = NULL;
    runtime_->mesh_pub = NULL;
    runtime_->mesh_xsub = NULL;
    runtime_->pub_ingress_sub = NULL;
    runtime_->peer_ctrl_pub = NULL;
    runtime_->peer_ctrl_sub = NULL;
    runtime_->routed_router = NULL;
    runtime_->local_fanout_xpub = NULL;
}

int spot_data_plane_t::initialize_runtime (spot_node_t *node_,
                                           spot_runtime_t *runtime_,
                                           spot_data_plane_runtime_state_t *state_out_)
{
    if (!node_ || !runtime_ || !state_out_) {
        errno = EINVAL;
        return -1;
    }

    const bool pubsub_enabled = spot_node_access_t::pubsub_enabled (node_);
    const bool routed_enabled = spot_node_access_t::routed_enabled (node_);

    state_out_->ctrl = spot_node_access_t::create_socket (node_, ZLINK_CORE_SOCKET_PAIR);
    if (pubsub_enabled) {
        state_out_->mesh_pub = spot_node_access_t::create_socket (node_, ZLINK_CORE_SOCKET_PUB);
        state_out_->mesh_xsub = spot_node_access_t::create_socket (node_, ZLINK_CORE_SOCKET_XSUB);
        state_out_->pub_ingress_sub =
          spot_node_access_t::create_socket (node_, ZLINK_CORE_SOCKET_SUB);
        state_out_->fanout = spot_node_access_t::create_socket (node_, ZLINK_CORE_SOCKET_PUB);
    }
    state_out_->peer_ctrl_pub = spot_node_access_t::create_socket (node_, ZLINK_CORE_SOCKET_PUB);
    state_out_->peer_ctrl_sub = spot_node_access_t::create_socket (node_, ZLINK_CORE_SOCKET_SUB);
    if (routed_enabled) {
        state_out_->routed_router =
          spot_node_access_t::create_socket (node_, ZLINK_CORE_SOCKET_ROUTER);
    }

    if (!state_out_->ctrl
        || (pubsub_enabled
            && (!state_out_->mesh_pub || !state_out_->mesh_xsub || !state_out_->pub_ingress_sub
                || !state_out_->fanout))
        || !state_out_->peer_ctrl_pub || !state_out_->peer_ctrl_sub
        || (routed_enabled && !state_out_->routed_router)) {
        const int err = errno != 0 ? errno : ENOMEM;
        if (state_out_->ctrl) {
            (void) state_out_->ctrl->connect (runtime_->data_ctrl_endpoint.c_str ());
            (void) spot_data_plane_protocol_t::send_errno_reply (state_out_->ctrl, err);
        }
        close_runtime_sockets (node_, state_out_);
        {
            scoped_lock_t lock (spot_node_access_t::sync (node_));
            clear_runtime_socket_refs (runtime_);
            runtime_->mark_fault (err);
        }
        spot_data_plane_protocol_t::clear_mesh_connected_endpoints (runtime_);
        errno = err;
        return -1;
    }

    state_out_->ctrl->set_auto_hwm_policy_enabled (false);
    if (state_out_->mesh_pub)
        state_out_->mesh_pub->set_auto_hwm_policy_enabled (false);
    if (state_out_->mesh_xsub)
        state_out_->mesh_xsub->set_auto_hwm_policy_enabled (false);
    if (state_out_->pub_ingress_sub)
        state_out_->pub_ingress_sub->set_auto_hwm_policy_enabled (false);
    state_out_->peer_ctrl_pub->set_auto_hwm_policy_enabled (false);
    state_out_->peer_ctrl_sub->set_auto_hwm_policy_enabled (false);
    if (state_out_->routed_router)
        state_out_->routed_router->set_auto_hwm_policy_enabled (false);
    if (state_out_->fanout)
        state_out_->fanout->set_auto_hwm_policy_enabled (false);

    {
        scoped_lock_t lock (spot_node_access_t::sync (node_));
        runtime_->data_ctrl_back = state_out_->ctrl;
        runtime_->mesh_pub = state_out_->mesh_pub;
        runtime_->mesh_xsub = state_out_->mesh_xsub;
        runtime_->pub_ingress_sub = state_out_->pub_ingress_sub;
        runtime_->peer_ctrl_pub = state_out_->peer_ctrl_pub;
        runtime_->peer_ctrl_sub = state_out_->peer_ctrl_sub;
        runtime_->routed_router = state_out_->routed_router;
        runtime_->local_fanout_xpub = state_out_->fanout;
        spot_node_access_t::track_owned_socket (node_, state_out_->ctrl);
        spot_node_access_t::track_owned_socket (node_, state_out_->mesh_pub);
        spot_node_access_t::track_owned_socket (node_, state_out_->mesh_xsub);
        spot_node_access_t::track_owned_socket (node_, state_out_->pub_ingress_sub);
        spot_node_access_t::track_owned_socket (node_, state_out_->peer_ctrl_pub);
        spot_node_access_t::track_owned_socket (node_, state_out_->peer_ctrl_sub);
        spot_node_access_t::track_owned_socket (node_, state_out_->routed_router);
        spot_node_access_t::track_owned_socket (node_, state_out_->fanout);
    }

    spot_data_plane_configure_runtime_sockets (runtime_, state_out_);

    if (state_out_->routed_router
        && zlink_spot_install_routed_router_dispatch (node_, state_out_->routed_router) != 0) {
        const int err = errno != 0 ? errno : EIO;
        (void) spot_data_plane_protocol_t::send_errno_reply (state_out_->ctrl, err);
        close_runtime_sockets (node_, state_out_);
        {
            scoped_lock_t lock (spot_node_access_t::sync (node_));
            clear_runtime_socket_refs (runtime_);
            runtime_->mark_fault (err);
        }
        spot_data_plane_protocol_t::clear_mesh_connected_endpoints (runtime_);
        errno = err;
        return -1;
    }

    if (open_mesh_peer_observer (state_out_) != 0) {
        const int err = errno != 0 ? errno : EIO;
        (void) spot_data_plane_protocol_t::send_errno_reply (state_out_->ctrl, err);
        close_runtime_sockets (node_, state_out_);
        {
            scoped_lock_t lock (spot_node_access_t::sync (node_));
            clear_runtime_socket_refs (runtime_);
            runtime_->mark_fault (err);
        }
        spot_data_plane_protocol_t::clear_mesh_connected_endpoints (runtime_);
        errno = err;
        return -1;
    }

    state_out_->poller = new (std::nothrow) socket_poller_t ();
    if (!state_out_->poller || state_out_->poller->add (state_out_->ctrl, NULL, ZLINK_POLLIN) != 0
        || (state_out_->mesh_pub && state_out_->poller->add (state_out_->mesh_pub, NULL, 0) != 0)
        || (state_out_->mesh_xsub
            && state_out_->poller->add (state_out_->mesh_xsub, NULL, ZLINK_POLLIN) != 0)
        || (state_out_->pub_ingress_sub
            && state_out_->poller->add (state_out_->pub_ingress_sub, NULL, ZLINK_POLLIN) != 0)
        || (state_out_->peer_ctrl_sub
            && state_out_->poller->add (state_out_->peer_ctrl_sub, NULL, ZLINK_POLLIN) != 0)
        || (state_out_->routed_router
            && state_out_->poller->add (state_out_->routed_router, NULL, ZLINK_POLLIN) != 0)
        || add_mesh_peer_observer_to_poller (state_out_) != 0
        || (state_out_->publish_ingress.signaler.valid ()
            && state_out_->poller->add_fd (state_out_->publish_ingress.signaler.get_fd (), NULL,
                                           ZLINK_POLLIN)
                 != 0)
        || (state_out_->routed_send.signaler.valid ()
            && state_out_->poller->add_fd (state_out_->routed_send.signaler.get_fd (), NULL,
                                           ZLINK_POLLIN)
                 != 0)
        || (state_out_->routed_router_ingress.signaler.valid ()
            && state_out_->poller->add_fd (state_out_->routed_router_ingress.signaler.get_fd (),
                                           NULL, ZLINK_POLLIN)
                 != 0)) {
        const int err = errno != 0 ? errno : ENOMEM;
        (void) spot_data_plane_protocol_t::send_errno_reply (state_out_->ctrl, err);
        close_runtime_sockets (node_, state_out_);
        {
            scoped_lock_t lock (spot_node_access_t::sync (node_));
            clear_runtime_socket_refs (runtime_);
            runtime_->mark_fault (err);
        }
        spot_data_plane_protocol_t::clear_mesh_connected_endpoints (runtime_);
        errno = err;
        return -1;
    }

    if ((state_out_->mesh_xsub
         && subscribe_runtime_mesh_topics (runtime_, state_out_->mesh_xsub) != 0)
        || (state_out_->pub_ingress_sub
            && state_out_->pub_ingress_sub->bind (runtime_->pub_ingress_endpoint.c_str ()) != 0)
        || (state_out_->fanout
            && state_out_->fanout->bind (runtime_->sub_fanout_endpoint.c_str ()) != 0)) {
        const int err = errno != 0 ? errno : EIO;
        (void) spot_data_plane_protocol_t::send_errno_reply (state_out_->ctrl, err);
        if (state_out_->mesh_pub)
            (void) state_out_->mesh_pub->monitor (NULL, 0, 3, ZLINK_CORE_SOCKET_PAIR);
        if (state_out_->mesh_xsub)
            (void) state_out_->mesh_xsub->monitor (NULL, 0, 3, ZLINK_CORE_SOCKET_PAIR);
        close_runtime_sockets (node_, state_out_);
        {
            scoped_lock_t lock (spot_node_access_t::sync (node_));
            clear_runtime_socket_refs (runtime_);
            runtime_->mark_fault (err);
        }
        spot_data_plane_protocol_t::clear_mesh_connected_endpoints (runtime_);
        errno = err;
        return -1;
    }

    if (spot_data_plane_protocol_t::send_ok_reply (state_out_->ctrl) != 0) {
        const int err = errno != 0 ? errno : EIO;
        if (state_out_->mesh_pub)
            (void) state_out_->mesh_pub->monitor (NULL, 0, 3, ZLINK_CORE_SOCKET_PAIR);
        if (state_out_->mesh_xsub)
            (void) state_out_->mesh_xsub->monitor (NULL, 0, 3, ZLINK_CORE_SOCKET_PAIR);
        close_runtime_sockets (node_, state_out_);
        {
            scoped_lock_t lock (spot_node_access_t::sync (node_));
            clear_runtime_socket_refs (runtime_);
            runtime_->mark_fault (err);
        }
        spot_data_plane_protocol_t::clear_mesh_connected_endpoints (runtime_);
        errno = err;
        return -1;
    }

    return 0;
}

void spot_data_plane_t::teardown_runtime (spot_node_t *node_,
                                          spot_runtime_t *runtime_,
                                          spot_data_plane_runtime_state_t *state_,
                                          spot_data_plane_protocol_state_t *protocol_state_)
{
    if (!node_ || !runtime_ || !state_)
        return;

    if (state_->routed_router && state_->routed_router->socket_msg_dispatch_active ())
        (void) state_->routed_router->socket_msg_dispatch_stop ();

    spot_data_plane_protocol_t::clear_snapshot_sources (node_, protocol_state_);
    if (protocol_state_) {
        protocol_state_->outbound_ready_filters.clear ();
        for (std::map<std::string, std::string>::const_iterator it =
               protocol_state_->peer_ctrl_endpoints.begin ();
             it != protocol_state_->peer_ctrl_endpoints.end (); ++it) {
            if (!it->second.empty () && state_->peer_ctrl_pub)
                (void) state_->peer_ctrl_pub->term_endpoint (it->second.c_str ());
            if (!it->first.empty () && state_->mesh_xsub)
                (void) state_->mesh_xsub->term_endpoint (it->first.c_str ());
        }
        protocol_state_->peer_ctrl_endpoints.clear ();
    }

    {
        std::lock_guard<std::mutex> lock (state_->publish_ingress.mutex);
        state_->publish_ingress.closed = true;
        while (!state_->publish_ingress.messages.empty ()) {
            spot_clear_msg_parts (&state_->publish_ingress.messages.front ().parts);
            state_->publish_ingress.messages.pop_front ();
        }
        state_->publish_ingress.queued_bytes = 0;
        state_->publish_ingress.signal_armed = false;
        state_->publish_ingress.cv.notify_all ();
    }
    {
        std::lock_guard<std::mutex> lock (state_->routed_send.mutex);
        state_->routed_send.closed = true;
        while (!state_->routed_send.messages.empty ()) {
            zlink::request_reply::close_built_parts (&state_->routed_send.messages.front ().parts);
            state_->routed_send.messages.pop_front ();
        }
        state_->routed_send.retry_after_ms = 0;
        state_->routed_send.signal_armed = false;
        state_->routed_send.cv.notify_all ();
    }
    {
        std::lock_guard<std::mutex> lock (state_->routed_router_ingress.mutex);
        state_->routed_router_ingress.closed = true;
        while (!state_->routed_router_ingress.messages.empty ()) {
            zlink::request_reply::close_built_parts (
              &state_->routed_router_ingress.messages.front ().parts);
            state_->routed_router_ingress.messages.pop_front ();
        }
        state_->routed_router_ingress.signal_armed = false;
    }

    if (state_) {
        for (spot_data_plane_runtime_state_t::remote_mesh_state_t::target_map_t::iterator it =
               state_->remote_mesh.targets.begin ();
             it != state_->remote_mesh.targets.end (); ++it) {
            if (state_->poller && it->second.sender_socket)
                (void) state_->poller->remove (it->second.sender_socket);
            if (it->second.sender_socket && !it->second.route_endpoint.empty ())
                (void) it->second.sender_socket->term_endpoint (it->second.route_endpoint.c_str ());
            if (it->second.sender_socket)
                (void) spot_node_access_t::close_owned_socket_and_wait (
                  runtime_->owner, it->second.sender_socket, 1000);
        }
        state_->remote_mesh.targets.clear ();
        state_->remote_mesh.pending_messages.clear ();
    }

    spot_data_plane_debug_logf ("delete poller begin\n");
    LIBZLINK_DELETE (state_->poller);
    state_->poller = NULL;
    spot_data_plane_debug_logf ("delete poller end\n");

    spot_data_plane_debug_logf ("close observer begin\n");
    close_mesh_peer_observer (node_, state_);
    spot_data_plane_debug_logf ("close observer end\n");

    {
        scoped_lock_t lock (spot_node_access_t::sync (node_));
        runtime_->peer_ctrl_endpoint.clear ();
        runtime_->bound_endpoint.clear ();
    }
    (void) runtime_->clear_external_route_ids ();
    spot_mesh_pub_hwm_t::reset_runtime_state (runtime_);
    spot_data_plane_protocol_t::clear_mesh_connected_endpoints (runtime_);
}
}
