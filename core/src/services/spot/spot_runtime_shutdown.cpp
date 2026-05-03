/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_runtime_internal.hpp"

#include "services/control/service_control_runtime.hpp"
#include "services/spot/spot_data_plane.hpp"
#include "services/spot/spot_node.hpp"

namespace zlink
{
namespace
{
void stop_runtime_dispatch_local (socket_base_t *socket_)
{
    if (!socket_)
        return;
    if (socket_->socket_msg_dispatch_active ())
        (void) socket_->socket_msg_dispatch_stop ();
    if (socket_->sub_dispatch_active ())
        (void) socket_->sub_dispatch_stop ();
}
}

void spot_runtime_t::stop_sockets ()
{
    runtime_socket_slot_ref_t refs[11];
    socket_base_t *sockets[11];
    const size_t slot_count = fill_runtime_socket_slot_refs (this, refs);

    {
        scoped_lock_t lock (owner->_sync);
        for (size_t i = 0; i < slot_count; ++i)
            sockets[i] = refs[i].slot ? *refs[i].slot : NULL;
    }

    for (size_t i = 0; i < slot_count; ++i) {
        if (sockets[i])
            sockets[i]->stop ();
    }
}

int spot_runtime_t::close_control_sockets ()
{
    int first_error = 0;
    runtime_socket_slot_ref_t refs[11];
    socket_base_t *sockets[11];
    const std::string *endpoints[11];
    const bool close_directly[11] = {
      false, false, false, false, false, false, false, false, true, false,
      false};
    const size_t slot_count = fill_runtime_socket_slot_refs (this, refs);
    {
        scoped_lock_t lock (owner->_sync);
        for (size_t i = 0; i < slot_count; ++i) {
            sockets[i] = refs[i].slot ? *refs[i].slot : NULL;
            endpoints[i] = refs[i].endpoint;
            if (refs[i].slot)
                *refs[i].slot = NULL;
        }
    }

    if (spot_shutdown_debug_enabled ()) {
        std::fprintf (
          stderr,
          "[spot-close] ctrl_front=%d ctrl_back=%d mesh_pub=%d mesh_xsub=%d peer_ctrl_pub=%d peer_ctrl_sub=%d external_router=%d internal_router=%d internal_router_tx=%d ingress=%d fanout=%d\n",
          sockets[0] ? sockets[0]->socket_id () : -1,
          sockets[1] ? sockets[1]->socket_id () : -1,
          sockets[2] ? sockets[2]->socket_id () : -1,
          sockets[3] ? sockets[3]->socket_id () : -1,
          sockets[4] ? sockets[4]->socket_id () : -1,
          sockets[5] ? sockets[5]->socket_id () : -1,
          sockets[6] ? sockets[6]->socket_id () : -1,
          sockets[7] ? sockets[7]->socket_id () : -1,
          sockets[8] ? sockets[8]->socket_id () : -1,
          sockets[9] ? sockets[9]->socket_id () : -1,
          sockets[10] ? sockets[10]->socket_id () : -1);
        std::fflush (stderr);
    }

    if (owner && owner->_ctx) {
        for (size_t i = 0; i < slot_count; ++i) {
            if (sockets[i] && endpoints[i] && !endpoints[i]->empty ())
                (void) sockets[i]->term_endpoint (endpoints[i]->c_str ());
        }
        if (sockets[7] && !external_router_bind_endpoint.empty ())
            (void) sockets[7]->term_endpoint (external_router_bind_endpoint.c_str ());
        for (size_t i = 0; i < slot_count; ++i) {
            if (sockets[i])
                sockets[i]->set_all_pipes_nodelay ();
        }
        stop_runtime_dispatch_local (sockets[6]);
        stop_runtime_dispatch_local (sockets[7]);
        stop_runtime_dispatch_local (sockets[8]);
        for (size_t i = 0; i < slot_count; ++i) {
            if (!sockets[i])
                continue;
            if (close_directly[i]) {
                close_socket_ptr (&sockets[i]);
                continue;
            }
            preserve_first_error (
              close_runtime_socket_async (sockets[i], 2000), &first_error);
        }
    } else {
        for (size_t i = 0; i < slot_count; ++i)
            close_socket_ptr (&sockets[i]);
    }

    if (first_error != 0) {
        errno = first_error;
        return -1;
    }
    return 0;
}

int spot_runtime_t::detach_runtime_endpoints ()
{
    socket_base_t *ctrl_front = NULL;
    socket_base_t *ctrl_back = NULL;
    socket_base_t *external_router_local = NULL;
    socket_base_t *internal_router_local = NULL;
    socket_base_t *ingress = NULL;
    socket_base_t *fanout = NULL;
    socket_base_t *internal_router_tx_local = NULL;
    std::string internal_router_sender_endpoint_local;

    {
        scoped_lock_t lock (owner->_sync);
        ctrl_front = data_ctrl_front;
        ctrl_back = data_ctrl_back;
        external_router_local = external_router;
        internal_router_local = internal_router;
        internal_router_tx_local = internal_router_tx;
        ingress = local_pub_ingress_sub;
        fanout = local_fanout_xpub;
        internal_router_sender_endpoint_local = internal_router_sender_endpoint;
    }

    if (ctrl_front && !data_ctrl_endpoint.empty ())
        (void) ctrl_front->term_endpoint (data_ctrl_endpoint.c_str ());
    if (ctrl_back && !data_ctrl_endpoint.empty ())
        (void) ctrl_back->term_endpoint (data_ctrl_endpoint.c_str ());
    if (internal_router_local && !internal_router_endpoint.empty ())
        (void) internal_router_local->term_endpoint (internal_router_endpoint.c_str ());
    if (external_router_local && !external_router_bind_endpoint.empty ())
        (void) external_router_local->term_endpoint (
          external_router_bind_endpoint.c_str ());
    if (ingress && !pub_ingress_endpoint.empty ())
        (void) ingress->term_endpoint (pub_ingress_endpoint.c_str ());
    if (fanout && !sub_fanout_endpoint.empty ())
        (void) fanout->term_endpoint (sub_fanout_endpoint.c_str ());
    if (internal_router_tx_local && !internal_router_sender_endpoint_local.empty ())
        (void) internal_router_tx_local->term_endpoint (
          internal_router_sender_endpoint_local.c_str ());
    return 0;
}

int spot_runtime_t::stop_and_join ()
{
    begin_shutdown ();
    if (data_ctrl_front) {
        scoped_lock_t lock (ctrl_sync);
        if (send_ascii_frame (data_ctrl_front, "terminate", 0) != 0) {
            const int err = errno != 0 ? errno : EIO;
            if (err != EAGAIN && err != ETIMEDOUT && err != EFSM && err != ETERM
                && err != EPIPE && err != ENOTSOCK) {
                errno = err;
                return -1;
            }
        }
    }
    advance_shutdown_phase (spot_shutdown_phase_stop_producers);
    (void) close_sender_caches (1000);
    (void) detach_runtime_endpoints ();
    advance_shutdown_phase (spot_shutdown_phase_detach_endpoints);
    stop_sockets ();
    if (execution.data_plane_running) {
        const uint64_t task_id = clear_data_plane_task_id ();
        if (task_id != 0 && data_plane_runtime)
            (void) data_plane_runtime->remove_task (task_id);
        spot_data_plane_t::teardown_runtime (
          owner, this, &execution.data_plane_state,
          &execution.data_plane_protocol_state);
        execution.data_plane_running = false;
    }
    data_plane_runtime = NULL;
    std::vector<socket_base_t *> retired_relay_sockets;
    {
        scoped_lock_t lock (attachment_sync);
        while (!retired_attachment_relay_sockets.empty ()) {
            retired_relay_sockets.push_back (
              retired_attachment_relay_sockets.front ());
            retired_attachment_relay_sockets.pop_front ();
        }
    }
    for (size_t i = 0; i < retired_relay_sockets.size (); ++i) {
        socket_base_t *socket = retired_relay_sockets[i];
        if (!socket)
            continue;
        socket->set_all_pipes_nodelay ();
        preserve_first_error (close_runtime_socket (socket, 1000), NULL);
    }
    advance_shutdown_phase (spot_shutdown_phase_close_transports);
    advance_shutdown_phase (spot_shutdown_phase_drain_state);
    advance_shutdown_phase (spot_shutdown_phase_cleanup);
    return 0;
}

size_t spot_runtime_t::live_socket_slot_count () const
{
    size_t count = 0;
    const_runtime_socket_slot_ref_t refs[14];
    const size_t slot_count =
      fill_runtime_socket_slot_refs (this, refs);
    scoped_lock_t lock (owner->_sync);
    for (size_t i = 0; i < slot_count; ++i)
        count += refs[i].slot && *refs[i].slot != NULL ? 1 : 0;
    return count;
}

size_t spot_runtime_t::attachment_count () const
{
    scoped_lock_t lock (attachment_sync);
    return attachments.size ();
}

int spot_runtime_t::abortive_stop ()
{
    abortive_shutdown = true;
    begin_shutdown ();
    stop_sockets ();
    if (execution.data_plane_running) {
        const uint64_t task_id = clear_data_plane_task_id ();
        if (task_id != 0 && data_plane_runtime)
            (void) data_plane_runtime->remove_task (task_id);
        spot_data_plane_t::teardown_runtime (
          owner, this, &execution.data_plane_state,
          &execution.data_plane_protocol_state);
        execution.data_plane_running = false;
    }
    data_plane_runtime = NULL;
    advance_shutdown_phase (spot_shutdown_phase_stop_producers);
    (void) close_sender_caches (1000);
    (void) detach_runtime_endpoints ();
    advance_shutdown_phase (spot_shutdown_phase_detach_endpoints);

    socket_base_t *ctrl_front = NULL;
    socket_base_t *ctrl_back = NULL;
    socket_base_t *mesh_pub_local = NULL;
    socket_base_t *mesh_xsub_local = NULL;
    socket_base_t *peer_ctrl_pub_local = NULL;
    socket_base_t *peer_ctrl_sub_local = NULL;
    socket_base_t *external_router_local = NULL;
    socket_base_t *internal_router_local = NULL;
    socket_base_t *internal_router_tx_local = NULL;
    socket_base_t *ingress = NULL;
    socket_base_t *fanout = NULL;
    {
        scoped_lock_t lock (owner->_sync);
        ctrl_front = data_ctrl_front;
        ctrl_back = data_ctrl_back;
        mesh_pub_local = mesh_pub;
        mesh_xsub_local = mesh_xsub;
        peer_ctrl_pub_local = peer_ctrl_pub;
        peer_ctrl_sub_local = peer_ctrl_sub;
        external_router_local = external_router;
        internal_router_local = internal_router;
        internal_router_tx_local = internal_router_tx;
        ingress = local_pub_ingress_sub;
        fanout = local_fanout_xpub;
        data_ctrl_front = NULL;
        data_ctrl_back = NULL;
        mesh_pub = NULL;
        mesh_xsub = NULL;
        peer_ctrl_pub = NULL;
        peer_ctrl_sub = NULL;
        external_router = NULL;
        internal_router = NULL;
        internal_router_tx = NULL;
        local_pub_ingress_sub = NULL;
        local_fanout_xpub = NULL;
    }
    (void) clear_external_route_ids ();

    std::vector<socket_base_t *> attachment_sockets;
    {
        scoped_lock_t lock (attachment_sync);
        for (std::map<uint64_t, spot_attachment_t>::iterator it =
               attachments.begin ();
             it != attachments.end (); ++it) {
            if (it->second.socket)
                attachment_sockets.push_back (it->second.socket);
            if (it->second.relay_socket)
                attachment_sockets.push_back (it->second.relay_socket);
        }
        while (!retired_attachment_relay_sockets.empty ()) {
            attachment_sockets.push_back (
              retired_attachment_relay_sockets.front ());
            retired_attachment_relay_sockets.pop_front ();
        }
        attachments.clear ();
    }

    if (owner && owner->_ctx) {
        if (ctrl_front)
            ctrl_front->set_all_pipes_nodelay ();
        if (ctrl_back)
            ctrl_back->set_all_pipes_nodelay ();
        if (mesh_pub_local)
            mesh_pub_local->set_all_pipes_nodelay ();
        if (mesh_xsub_local)
            mesh_xsub_local->set_all_pipes_nodelay ();
        if (peer_ctrl_pub_local)
            peer_ctrl_pub_local->set_all_pipes_nodelay ();
        if (peer_ctrl_sub_local)
            peer_ctrl_sub_local->set_all_pipes_nodelay ();
        if (external_router_local)
            external_router_local->set_all_pipes_nodelay ();
        if (internal_router_local)
            internal_router_local->set_all_pipes_nodelay ();
        if (internal_router_tx_local)
            internal_router_tx_local->set_all_pipes_nodelay ();
        if (ingress)
            ingress->set_all_pipes_nodelay ();
        if (fanout)
            fanout->set_all_pipes_nodelay ();
        stop_runtime_dispatch_local (external_router_local);
        stop_runtime_dispatch_local (internal_router_local);
        (void) close_runtime_socket (ctrl_front, 1000);
        (void) close_runtime_socket (ctrl_back, 1000);
        (void) close_runtime_socket (mesh_pub_local, 1000);
        (void) close_runtime_socket (mesh_xsub_local, 1000);
        (void) close_runtime_socket (peer_ctrl_pub_local, 1000);
        (void) close_runtime_socket (peer_ctrl_sub_local, 1000);
        (void) close_runtime_socket (external_router_local, 1000);
        (void) close_runtime_socket (internal_router_local, 1000);
        (void) close_runtime_socket (internal_router_tx_local, 1000);
        (void) close_runtime_socket (ingress, 1000);
        (void) close_runtime_socket (fanout, 1000);
        for (size_t i = 0; i < attachment_sockets.size (); ++i) {
            socket_base_t *socket = attachment_sockets[i];
            (void) close_runtime_socket (socket, 1000);
        }
    } else {
        close_socket_ptr (&ctrl_front);
        close_socket_ptr (&ctrl_back);
        close_socket_ptr (&mesh_pub_local);
        close_socket_ptr (&mesh_xsub_local);
        close_socket_ptr (&peer_ctrl_pub_local);
        close_socket_ptr (&peer_ctrl_sub_local);
        close_socket_ptr (&external_router_local);
        close_socket_ptr (&internal_router_local);
        close_socket_ptr (&internal_router_tx_local);
        close_socket_ptr (&ingress);
        close_socket_ptr (&fanout);
        for (size_t i = 0; i < attachment_sockets.size (); ++i) {
            socket_base_t *socket = attachment_sockets[i];
            close_socket_ptr (&socket);
        }
    }

    advance_shutdown_phase (spot_shutdown_phase_close_transports);
    advance_shutdown_phase (spot_shutdown_phase_drain_state);
    advance_shutdown_phase (spot_shutdown_phase_cleanup);
    return 0;
}
}
