/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_runtime_internal.hpp"
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

    const int socket_type =
      kind_ == spot_attachment_pub ? ZLINK_CORE_SOCKET_PUB
                                   : ZLINK_CORE_SOCKET_SUB;
    socket_base_t *socket = owner->_ctx->create_socket (socket_type);
    if (!socket)
        return -1;

    owner->track_owned_socket (socket);
    if (socket->connect (endpoint_) != 0) {
        (void) close_runtime_socket (socket, 1000);
        return -1;
    }

    spot_attachment_t attachment;
    {
        scoped_lock_t lock (attachment_sync);
        attachment.id = ++next_attachment_id;
        if (attachment.id == 0)
            attachment.id = ++next_attachment_id;
        attachment.kind = kind_;
        attachment.socket = socket;
        attachment.endpoint = endpoint_;
        attachments[attachment.id] = attachment;
    }
    *out_id_ = attachment.id;
    return 0;
}

int spot_runtime_t::close_runtime_socket (socket_base_t *&socket_,
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
    std::string endpoint;
    {
        scoped_lock_t lock (attachment_sync);
        std::map<uint64_t, spot_attachment_t>::iterator it =
          attachments.find (id_);
        if (it == attachments.end ())
            return 0;
        socket = it->second.socket;
        endpoint = it->second.endpoint;
        attachments.erase (it);
    }

    if (!socket)
        return 0;
    spot_runtime_diag_logf_local ("destroy_attachment id=%llu socket=%d endpoint=%s",
                                  static_cast<unsigned long long> (id_),
                                  socket->socket_id (), endpoint.c_str ());
    if (!endpoint.empty ())
        (void) socket->term_endpoint (endpoint.c_str ());
    socket->set_all_pipes_nodelay ();
    return close_runtime_socket (socket, 10000);
}

int spot_runtime_t::destroy_attachment_async (uint64_t id_)
{
    if (id_ == 0)
        return 0;

    socket_base_t *socket = NULL;
    std::string endpoint;
    {
        scoped_lock_t lock (attachment_sync);
        std::map<uint64_t, spot_attachment_t>::iterator it =
          attachments.find (id_);
        if (it == attachments.end ())
            return 0;
        socket = it->second.socket;
        endpoint = it->second.endpoint;
        attachments.erase (it);
    }

    if (!socket)
        return 0;
    spot_runtime_diag_logf_local (
      "destroy_attachment_async id=%llu socket=%d endpoint=%s",
      static_cast<unsigned long long> (id_), socket->socket_id (),
      endpoint.c_str ());
    if (!endpoint.empty ())
        (void) socket->term_endpoint (endpoint.c_str ());
    socket->set_all_pipes_nodelay ();
    return close_runtime_socket_async (socket, 10000);
}
}
