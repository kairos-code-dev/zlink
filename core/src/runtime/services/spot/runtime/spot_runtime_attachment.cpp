/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/runtime/spot_runtime_internal.hpp"
#include "services/spot/common/spot_auto_hwm_internal.hpp"
#include "services/spot/common/spot_debug.hpp"
#include "services/spot/data_plane/spot_data_plane_internal.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/node/spot_node_access.hpp"

#include <vector>

namespace zlink
{
namespace
{
void spot_runtime_diag_logf_local (const char *fmt_, ...)
{
    va_list args;
    va_start (args, fmt_);
    debug_vfprintf ("ZLINK_DEBUG_SPOT_RUNTIME_ATTACH", "[spot-runtime] ", fmt_, args);
    va_end (args);
}

std::string make_sub_attachment_relay_endpoint (const spot_runtime_t *runtime_,
                                                uint64_t attachment_id_)
{
    char buf[128];
    std::snprintf (buf, sizeof (buf), "inproc://spot-fanout-%u-%llu",
                   runtime_ ? runtime_->node_id : 0u,
                   static_cast<unsigned long long> (attachment_id_));
    return std::string (buf);
}
}

int spot_runtime_t::create_attachment (int kind_, const char *endpoint_, uint64_t *out_id_)
{
    ctx_t *owner_ctx = spot_node_access_t::ctx (owner);
    if (!owner_ctx || !endpoint_ || !out_id_) {
        errno = EFAULT;
        return -1;
    }
    if (kind_ != spot_attachment_pub && kind_ != spot_attachment_sub) {
        errno = EINVAL;
        return -1;
    }

    uint64_t attachment_id = 0;
    {
        scoped_lock_t lock (attachment_state.sync);
        attachment_id = ++attachment_state.next_id;
        if (attachment_id == 0)
            attachment_id = ++attachment_state.next_id;
    }

    const int socket_type =
      kind_ == spot_attachment_pub ? ZLINK_CORE_SOCKET_PUB : ZLINK_CORE_SOCKET_SUB;
    size_t local_pub_count = 0;
    size_t local_sub_count = 0;
    size_t connected_peer_count = 0;
    size_t active_peer_count = 0;
    snapshot_auto_hwm_inputs (&local_pub_count, &local_sub_count, &connected_peer_count,
                              &active_peer_count);
    const size_t next_local_pub_count =
      kind_ == spot_attachment_pub ? local_pub_count + 1 : local_pub_count;
    const size_t next_local_sub_count =
      kind_ == spot_attachment_sub ? local_sub_count + 1 : local_sub_count;
    const size_t next_spot_scope_count =
      std::max<size_t> (std::max<size_t> (next_local_pub_count, next_local_sub_count), 1u);
    socket_base_t *socket = spot_node_access_t::create_socket (owner, socket_type);
    if (!socket)
        return -1;
    socket_base_t *relay_socket = NULL;
    socket->set_auto_hwm_policy_enabled (false);
    const spot_node_runtime_tuning_t runtime_tuning = runtime_tuning_snapshot ();
    const int pubsub_admission_hwm = spot_node_pubsub_admission_hwm (runtime_tuning);
    const int zero = 0;
    apply_spot_internal_auto_hwm (
      owner_ctx, socket,
      kind_ == spot_attachment_pub
        ? spot_internal_auto_hwm_policy_t{auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB,
                                          std::max<size_t> (
                                            next_local_sub_count + connected_peer_count, 1u),
                                          std::max<size_t> (
                                            next_local_sub_count + active_peer_count, 1u),
                                          0, 0, true, true, auto_hwm_scope_per_spot,
                                          next_spot_scope_count, 0, false}
        : spot_internal_auto_hwm_policy_t{auto_hwm_role_recv_ingress, ZLINK_CORE_SOCKET_SUB,
                                          std::max<size_t> (next_local_sub_count, 1u),
                                          std::max<size_t> (next_local_sub_count, 1u), 0, 0, true,
                                          true, auto_hwm_scope_per_spot,
                                          next_spot_scope_count, 0, false});
    if (kind_ == spot_attachment_pub) {
        socket->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &pubsub_admission_hwm,
                            sizeof (pubsub_admission_hwm));
        socket->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &zero, sizeof (zero));
    } else {
        socket->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &zero, sizeof (zero));
        socket->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &zero, sizeof (zero));
    }

    spot_node_access_t::track_owned_socket (owner, socket);

    std::string relay_endpoint;
    if (kind_ == spot_attachment_sub) {
        relay_socket = spot_node_access_t::create_socket (owner, ZLINK_CORE_SOCKET_PUB);
        if (!relay_socket) {
            (void) close_runtime_socket (socket, 1000);
            return -1;
        }
        relay_socket->set_auto_hwm_policy_enabled (false);
        apply_spot_internal_auto_hwm (
          owner_ctx, relay_socket,
          spot_internal_auto_hwm_policy_t{auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB,
                                          std::max<size_t> (next_local_sub_count, 1u),
                                          std::max<size_t> (next_local_sub_count, 1u), 0, 0, true,
                                          true, auto_hwm_scope_per_spot,
                                          next_spot_scope_count, 0, false});
        relay_socket->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &zero, sizeof (zero));
        relay_socket->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &zero, sizeof (zero));
        const int neg_one = -1;
        relay_socket->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &neg_one, sizeof (neg_one));
        spot_node_access_t::track_owned_socket (owner, relay_socket);
        relay_endpoint = make_sub_attachment_relay_endpoint (this, attachment_id);
        if (relay_socket->bind (relay_endpoint.c_str ()) != 0) {
            const int err = errno != 0 ? errno : EIO;
            (void) close_runtime_socket (relay_socket, 1000);
            (void) close_runtime_socket (socket, 1000);
            errno = err;
            return -1;
        }
    }

    const char *connect_endpoint = relay_socket ? relay_endpoint.c_str () : endpoint_;
    if (socket->connect (connect_endpoint) != 0) {
        const int err = errno != 0 ? errno : EIO;
        if (relay_socket)
            (void) close_runtime_socket (relay_socket, 1000);
        (void) close_runtime_socket (socket, 1000);
        errno = err;
        return -1;
    }

    spot_attachment_t attachment;
    {
        scoped_lock_t lock (attachment_state.sync);
        attachment.id = attachment_id;
        attachment.kind = kind_;
        attachment.socket = socket;
        attachment.relay_socket = relay_socket;
        attachment.endpoint = endpoint_;
        attachment.relay_endpoint = relay_endpoint;
        attachment_state.items[attachment.id] = attachment;
        ++attachment_state.version;
    }
    refresh_attachment_auto_hwm_scope_counts ();
    *out_id_ = attachment.id;
    return 0;
}

int spot_runtime_t::close_runtime_socket (socket_base_t *&socket_, int timeout_ms_)
{
    if (!socket_)
        return 0;
    if (spot_node_access_t::ctx (owner))
        return spot_node_access_t::close_owned_socket_and_wait (owner, socket_, timeout_ms_);

    socket_->stop ();
    socket_->close ();
    socket_ = NULL;
    return 0;
}

int spot_runtime_t::close_runtime_socket_async (socket_base_t *&socket_, int timeout_ms_)
{
    if (!socket_)
        return 0;
    if (owner && spot_node_access_t::is_shutting_down (owner)) {
        spot_node_access_t::untrack_owned_socket (owner, socket_);
        socket_->stop ();
        socket_->close ();
        socket_ = NULL;
        return 0;
    }
    if (spot_node_access_t::ctx (owner))
        return spot_node_access_t::close_owned_socket (owner, socket_, timeout_ms_);

    socket_->stop ();
    socket_->close ();
    socket_ = NULL;
    return 0;
}

socket_base_t *spot_runtime_t::attachment_socket (uint64_t id_) const
{
    if (id_ == 0)
        return NULL;
    scoped_lock_t lock (attachment_state.sync);
    std::map<uint64_t, spot_attachment_t>::const_iterator it = attachment_state.items.find (id_);
    return it != attachment_state.items.end () ? it->second.socket : NULL;
}

int spot_runtime_t::destroy_attachment (uint64_t id_)
{
    if (id_ == 0)
        return 0;

    socket_base_t *socket = NULL;
    socket_base_t *relay_socket = NULL;
    std::string endpoint;
    std::string relay_endpoint;
    bool data_plane_running = false;
    {
        scoped_lock_t lock (attachment_state.sync);
        std::map<uint64_t, spot_attachment_t>::iterator it = attachment_state.items.find (id_);
        if (it == attachment_state.items.end ())
            return 0;
        socket = it->second.socket;
        relay_socket = it->second.relay_socket;
        endpoint = it->second.endpoint;
        relay_endpoint = it->second.relay_endpoint;
        attachment_state.items.erase (it);
        ++attachment_state.version;
    }
    refresh_attachment_auto_hwm_scope_counts ();
    {
        scoped_lock_t lock (execution_sync);
        data_plane_running = execution.data_plane_running;
    }
    if (relay_socket) {
        if (data_plane_running) {
            scoped_lock_t lock (attachment_state.sync);
            attachment_state.retired_relay_sockets.push_back (relay_socket);
            ++attachment_state.version;
        } else {
            relay_socket->set_all_pipes_nodelay ();
            (void) close_runtime_socket (relay_socket, 10000);
        }
    }

    if (!socket)
        return 0;
    spot_runtime_diag_logf_local ("destroy_attachment id=%llu socket=%d endpoint=%s",
                                  static_cast<unsigned long long> (id_), socket->socket_id (),
                                  endpoint.c_str ());
    if (!endpoint.empty ())
        (void) socket->term_endpoint (endpoint.c_str ());
    if (!relay_endpoint.empty ())
        (void) socket->term_endpoint (relay_endpoint.c_str ());
    socket->set_all_pipes_nodelay ();
    const int rc = data_plane_running ? close_runtime_socket_async (socket, 10000)
                                      : close_runtime_socket (socket, 10000);
    return rc;
}

int spot_runtime_t::destroy_attachment_async (uint64_t id_)
{
    if (id_ == 0)
        return 0;

    socket_base_t *socket = NULL;
    socket_base_t *relay_socket = NULL;
    std::string endpoint;
    std::string relay_endpoint;
    bool data_plane_running = false;
    {
        scoped_lock_t lock (attachment_state.sync);
        std::map<uint64_t, spot_attachment_t>::iterator it = attachment_state.items.find (id_);
        if (it == attachment_state.items.end ())
            return 0;
        socket = it->second.socket;
        relay_socket = it->second.relay_socket;
        endpoint = it->second.endpoint;
        relay_endpoint = it->second.relay_endpoint;
        attachment_state.items.erase (it);
        ++attachment_state.version;
    }
    refresh_attachment_auto_hwm_scope_counts ();
    {
        scoped_lock_t lock (execution_sync);
        data_plane_running = execution.data_plane_running;
    }
    if (relay_socket) {
        if (data_plane_running) {
            scoped_lock_t lock (attachment_state.sync);
            attachment_state.retired_relay_sockets.push_back (relay_socket);
            ++attachment_state.version;
        } else {
            relay_socket->set_all_pipes_nodelay ();
            (void) close_runtime_socket_async (relay_socket, 10000);
        }
    }

    if (!socket)
        return 0;
    spot_runtime_diag_logf_local ("destroy_attachment_async id=%llu socket=%d endpoint=%s",
                                  static_cast<unsigned long long> (id_), socket->socket_id (),
                                  endpoint.c_str ());
    if (!endpoint.empty ())
        (void) socket->term_endpoint (endpoint.c_str ());
    if (!relay_endpoint.empty ())
        (void) socket->term_endpoint (relay_endpoint.c_str ());
    socket->set_all_pipes_nodelay ();
    const int rc = close_runtime_socket_async (socket, 10000);
    return rc;
}

size_t spot_runtime_t::attachment_count_by_kind (int kind_) const
{
    size_t count = 0;
    scoped_lock_t lock (attachment_state.sync);
    for (std::map<uint64_t, spot_attachment_t>::const_iterator it = attachment_state.items.begin ();
         it != attachment_state.items.end (); ++it) {
        if (it->second.kind == kind_)
            ++count;
    }
    return count;
}

void spot_runtime_t::refresh_attachment_auto_hwm_scope_counts ()
{
    std::vector<socket_base_t *> sockets;
    size_t local_pub_count = 0;
    size_t local_sub_count = 0;
    {
        scoped_lock_t lock (attachment_state.sync);
        for (std::map<uint64_t, spot_attachment_t>::const_iterator it =
               attachment_state.items.begin ();
             it != attachment_state.items.end (); ++it) {
            if (it->second.kind == spot_attachment_pub)
                ++local_pub_count;
            else if (it->second.kind == spot_attachment_sub)
                ++local_sub_count;
            if (it->second.socket)
                sockets.push_back (it->second.socket);
            if (it->second.relay_socket)
                sockets.push_back (it->second.relay_socket);
        }
    }

    const size_t scope_count =
      std::max<size_t> (std::max<size_t> (local_pub_count, local_sub_count), 1u);
    for (std::vector<socket_base_t *>::iterator it = sockets.begin (); it != sockets.end (); ++it) {
        if (!*it)
            continue;
        (*it)->set_auto_hwm_scope (auto_hwm_scope_per_spot, scope_count);
        (*it)->refresh_auto_hwm_policy (true);
    }
}

void spot_runtime_t::snapshot_auto_hwm_inputs (size_t *local_pub_count_out_,
                                               size_t *local_sub_count_out_,
                                               size_t *connected_peer_count_out_,
                                               size_t *active_peer_count_out_) const
{
    size_t local_pub_count = 0;
    size_t local_sub_count = 0;
    {
        scoped_lock_t lock (attachment_state.sync);
        for (std::map<uint64_t, spot_attachment_t>::const_iterator it =
               attachment_state.items.begin ();
             it != attachment_state.items.end (); ++it) {
            if (it->second.kind == spot_attachment_pub)
                ++local_pub_count;
            else if (it->second.kind == spot_attachment_sub)
                ++local_sub_count;
        }
    }

    std::set<std::string> connected_endpoints;
    snapshot_connected_mesh_peer_endpoints (&execution.mesh_peer_state, &connected_endpoints);
    const size_t connected_peer_count = connected_endpoints.size ();
    const uint32_t ready_peer_count = connected_ready_peer_count (&execution.mesh_peer_state);
    const size_t active_peer_count = ready_peer_count > 0
                                       ? std::min<size_t> (ready_peer_count, connected_peer_count)
                                       : connected_peer_count;

    if (local_pub_count_out_)
        *local_pub_count_out_ = local_pub_count;
    if (local_sub_count_out_)
        *local_sub_count_out_ = local_sub_count;
    if (connected_peer_count_out_)
        *connected_peer_count_out_ = connected_peer_count;
    if (active_peer_count_out_)
        *active_peer_count_out_ = active_peer_count;
}
}
