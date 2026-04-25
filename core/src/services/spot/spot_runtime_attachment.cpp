/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_runtime_internal.hpp"
#include "services/spot/spot_auto_hwm_internal.hpp"
#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_node.hpp"

namespace zlink
{
namespace
{
void spot_runtime_diag_logf_local (const char *fmt_, ...)
{
    if (!std::getenv ("ZLINK_DEBUG_SPOT_RUNTIME_ATTACH"))
        return;

    va_list args;
    va_start (args, fmt_);
    std::fprintf (stderr, "[spot-runtime] ");
    std::vfprintf (stderr, fmt_, args);
    std::fprintf (stderr, "\n");
    std::fflush (stderr);
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

int spot_runtime_t::create_attachment (int kind_,
                                       const char *endpoint_,
                                       uint64_t *out_id_)
{
    if (!owner || !owner->_ctx || !endpoint_ || !out_id_) {
        errno = EFAULT;
        return -1;
    }
    if (kind_ != spot_attachment_pub && kind_ != spot_attachment_sub) {
        errno = EINVAL;
        return -1;
    }

    uint64_t attachment_id = 0;
    {
        scoped_lock_t lock (attachment_sync);
        attachment_id = ++next_attachment_id;
        if (attachment_id == 0)
            attachment_id = ++next_attachment_id;
    }

    const int socket_type =
      kind_ == spot_attachment_pub ? ZLINK_CORE_SOCKET_PUB
                                   : ZLINK_CORE_SOCKET_SUB;
    size_t local_pub_count = 0;
    size_t local_sub_count = 0;
    size_t connected_peer_count = 0;
    size_t active_peer_count = 0;
    snapshot_auto_hwm_inputs (&local_pub_count, &local_sub_count,
                              &connected_peer_count, &active_peer_count);
    const size_t next_local_pub_count =
      kind_ == spot_attachment_pub ? local_pub_count + 1 : local_pub_count;
    const size_t next_local_sub_count =
      kind_ == spot_attachment_sub ? local_sub_count + 1 : local_sub_count;
    socket_base_t *socket = owner->_ctx->create_socket (socket_type);
    if (!socket)
        return -1;
    socket_base_t *relay_socket = NULL;
    socket->set_auto_hwm_policy_enabled (false);
    apply_spot_internal_auto_hwm (
      owner->_ctx, socket,
      kind_ == spot_attachment_pub
        ? spot_internal_auto_hwm_policy_t{auto_hwm_role_fanout,
                                          ZLINK_CORE_SOCKET_PUB,
                                          std::max<size_t> (
                                            next_local_sub_count
                                              + connected_peer_count,
                                            1u),
                                          std::max<size_t> (
                                            next_local_sub_count
                                              + active_peer_count,
                                            1u),
                                          0, 0, true, true, true, true}
        : spot_internal_auto_hwm_policy_t{auto_hwm_role_recv_ingress,
                                          ZLINK_CORE_SOCKET_SUB,
                                          std::max<size_t> (
                                            next_local_sub_count, 1u),
                                          std::max<size_t> (
                                            next_local_sub_count, 1u),
                                          0, 0,
                                          true, true, true, true});

    owner->track_owned_socket (socket);

    std::string relay_endpoint;
    if (kind_ == spot_attachment_sub) {
        relay_socket = owner->_ctx->create_socket (ZLINK_CORE_SOCKET_PUB);
        if (!relay_socket) {
            (void) close_runtime_socket (socket, 1000);
            return -1;
        }
        relay_socket->set_auto_hwm_policy_enabled (false);
        apply_spot_internal_auto_hwm (
          owner->_ctx, relay_socket,
          spot_internal_auto_hwm_policy_t{auto_hwm_role_fanout,
                                          ZLINK_CORE_SOCKET_PUB,
                                          std::max<size_t> (
                                            next_local_sub_count, 1u),
                                          std::max<size_t> (
                                            next_local_sub_count, 1u),
                                          0, 0,
                                          true, true, true, true});
        const int neg_one = -1;
        relay_socket->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &neg_one,
                                  sizeof (neg_one));
        owner->track_owned_socket (relay_socket);
        relay_endpoint =
          make_sub_attachment_relay_endpoint (this, attachment_id);
        if (relay_socket->bind (relay_endpoint.c_str ()) != 0) {
            const int err = errno != 0 ? errno : EIO;
            (void) close_runtime_socket (relay_socket, 1000);
            (void) close_runtime_socket (socket, 1000);
            errno = err;
            return -1;
        }
    }

    const char *connect_endpoint =
      relay_socket ? relay_endpoint.c_str () : endpoint_;
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
        scoped_lock_t lock (attachment_sync);
        attachment.id = attachment_id;
        attachment.kind = kind_;
        attachment.socket = socket;
        attachment.relay_socket = relay_socket;
        attachment.endpoint = endpoint_;
        attachment.relay_endpoint = relay_endpoint;
        attachments[attachment.id] = attachment;
        ++attachment_version;
    }
    *out_id_ = attachment.id;
    return 0;
}

int spot_runtime_t::close_runtime_socket (socket_base_t *&socket_,
                                          int timeout_ms_)
{
    if (!socket_)
        return 0;
    if (owner && owner->_ctx)
        return owner->_lifecycle.close_socket_and_wait (socket_, timeout_ms_);

    socket_->stop ();
    socket_->close ();
    socket_ = NULL;
    return 0;
}

int spot_runtime_t::close_runtime_socket_async (socket_base_t *&socket_,
                                                int timeout_ms_)
{
    if (!socket_)
        return 0;
    if (owner && owner->is_shutting_down ()) {
        socket_->stop ();
        socket_->close ();
        owner->untrack_owned_socket (socket_);
        socket_ = NULL;
        return 0;
    }
    if (owner && owner->_ctx)
        return owner->_lifecycle.close_socket (socket_, timeout_ms_);

    socket_->stop ();
    socket_->close ();
    socket_ = NULL;
    return 0;
}

socket_base_t *spot_runtime_t::attachment_socket (uint64_t id_) const
{
    if (id_ == 0)
        return NULL;
    scoped_lock_t lock (const_cast<mutex_t &> (attachment_sync));
    std::map<uint64_t, spot_attachment_t>::const_iterator it =
      attachments.find (id_);
    return it != attachments.end () ? it->second.socket : NULL;
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
        scoped_lock_t lock (attachment_sync);
        std::map<uint64_t, spot_attachment_t>::iterator it =
          attachments.find (id_);
        if (it == attachments.end ())
            return 0;
        socket = it->second.socket;
        relay_socket = it->second.relay_socket;
        endpoint = it->second.endpoint;
        relay_endpoint = it->second.relay_endpoint;
        attachments.erase (it);
        ++attachment_version;
    }
    {
        scoped_lock_t lock (execution_sync);
        data_plane_running = execution.data_plane_running;
    }
    if (relay_socket) {
        if (data_plane_running) {
            scoped_lock_t lock (attachment_sync);
            retired_attachment_relay_sockets.push_back (relay_socket);
            ++attachment_version;
        } else {
            relay_socket->set_all_pipes_nodelay ();
            (void) close_runtime_socket (relay_socket, 10000);
        }
    }

    if (!socket)
        return 0;
    spot_runtime_diag_logf_local ("destroy_attachment id=%llu socket=%d endpoint=%s",
                                  static_cast<unsigned long long> (id_),
                                  socket->socket_id (), endpoint.c_str ());
    if (!endpoint.empty ())
        (void) socket->term_endpoint (endpoint.c_str ());
    if (!relay_endpoint.empty ())
        (void) socket->term_endpoint (relay_endpoint.c_str ());
    socket->set_all_pipes_nodelay ();
    const int rc =
      data_plane_running ? close_runtime_socket_async (socket, 10000)
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
        scoped_lock_t lock (attachment_sync);
        std::map<uint64_t, spot_attachment_t>::iterator it =
          attachments.find (id_);
        if (it == attachments.end ())
            return 0;
        socket = it->second.socket;
        relay_socket = it->second.relay_socket;
        endpoint = it->second.endpoint;
        relay_endpoint = it->second.relay_endpoint;
        attachments.erase (it);
        ++attachment_version;
    }
    {
        scoped_lock_t lock (execution_sync);
        data_plane_running = execution.data_plane_running;
    }
    if (relay_socket) {
        if (data_plane_running) {
            scoped_lock_t lock (attachment_sync);
            retired_attachment_relay_sockets.push_back (relay_socket);
            ++attachment_version;
        } else {
            relay_socket->set_all_pipes_nodelay ();
            (void) close_runtime_socket_async (relay_socket, 10000);
        }
    }

    if (!socket)
        return 0;
    spot_runtime_diag_logf_local (
      "destroy_attachment_async id=%llu socket=%d endpoint=%s",
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
    scoped_lock_t lock (const_cast<mutex_t &> (attachment_sync));
    for (std::map<uint64_t, spot_attachment_t>::const_iterator it =
           attachments.begin ();
         it != attachments.end (); ++it) {
        if (it->second.kind == kind_)
            ++count;
    }
    return count;
}

void spot_runtime_t::snapshot_auto_hwm_inputs (
  size_t *local_pub_count_out_,
  size_t *local_sub_count_out_,
  size_t *connected_peer_count_out_,
  size_t *active_peer_count_out_) const
{
    size_t local_pub_count = 0;
    size_t local_sub_count = 0;
    {
        scoped_lock_t lock (const_cast<mutex_t &> (attachment_sync));
        for (std::map<uint64_t, spot_attachment_t>::const_iterator it =
               attachments.begin ();
             it != attachments.end (); ++it) {
            if (it->second.kind == spot_attachment_pub)
                ++local_pub_count;
            else if (it->second.kind == spot_attachment_sub)
                ++local_sub_count;
        }
    }

    std::set<std::string> connected_endpoints;
    snapshot_connected_mesh_peer_endpoints (&execution.mesh_peer_state,
                                            &connected_endpoints);
    const size_t connected_peer_count = connected_endpoints.size ();
    const uint32_t ready_peer_count =
      connected_ready_peer_count (&execution.mesh_peer_state);
    const size_t active_peer_count =
      ready_peer_count > 0
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
