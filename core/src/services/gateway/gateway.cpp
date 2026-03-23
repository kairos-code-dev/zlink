/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/gateway/gateway.hpp"

#include "core/msg.hpp"
#include "core/pipe.hpp"
#include "core/recv_internal.hpp"
#include "core/send_internal.hpp"
#include "core/multipart_send_txn.hpp"
#include "services/common/advertise_endpoint.hpp"
#include "services/common/monitor_decode.hpp"
#include "services/common/socket_monitor_bridge.hpp"
#include "services/gateway/gateway_access.hpp"
#include "services/gateway/gateway_runtime.hpp"
#include "services/gateway/routing_id_utils.hpp"
#include "services/control/service_control_runtime.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "utils/sleep.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <cstdio>

#include <zlink.h>

namespace zlink
{
namespace
{
static void gateway_router_msg_handler (const zlink_routing_id_t *source_rid_,
                                        zlink_msg_t *parts_,
                                        size_t part_count_,
                                        void *)
{
    gateway_t *gateway = static_cast<gateway_t *> (
      socket_base_t::current_socket_msg_dispatch_subject ());
    if (!gateway) {
        for (size_t i = 0; i < part_count_; ++i)
            zlink_msg_close (&parts_[i]);
        return;
    }

    gateway_access_t::dispatch_message (gateway, source_rid_, parts_, part_count_);
} // namespace
} // namespace zlink

static void gateway_send_ready_handler (void *subject_, void *)
{
    gateway_t *gateway = static_cast<gateway_t *> (subject_);
    if (!gateway)
        return;
    gateway_access_t::dispatch_send_ready (gateway);
}

// Ensure the ROUTER socket has a routing id so peers can reply.
static void ensure_gateway_routing_id (socket_base_t *socket_,
                                       const std::string *override_id_)
{
    if (!socket_)
        return;
    if (override_id_ && !override_id_->empty ()) {
        // Explicit routing id must take precedence over any auto-generated id.
        zlink::discovery::set_socket_routing_id (socket_, override_id_, NULL);
        return;
    }
    unsigned char buf[256];
    size_t size = sizeof (buf);
    if (socket_->getsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, buf, &size) != 0)
        return;
    if (size > 0)
        return;
    zlink::discovery::set_socket_routing_id (socket_, override_id_, NULL);
}

static int allocate_router (ctx_t *ctx_, socket_base_t **socket_)
{
    *socket_ = ctx_->create_socket (ZLINK_CORE_SOCKET_ROUTER);
    if (!*socket_)
        return -1;
    return 0;
}

// Apply TLS client settings to the ROUTER socket (optional).
static int apply_tls_client (socket_base_t *socket_,
                             const std::string &ca_cert_,
                             const std::string &hostname_,
                             int trust_system_)
{
    if (!socket_)
        return -1;
    if (ca_cert_.empty () || hostname_.empty ())
        return 0;
    if (socket_->setsockopt (ZLINK_INTERNAL_OPT_TLS_CA, ca_cert_.data (),
                             ca_cert_.size ())
        != 0)
        return -1;
    if (socket_->setsockopt (ZLINK_INTERNAL_OPT_TLS_HOSTNAME, hostname_.data (),
                             hostname_.size ())
        != 0)
        return -1;
    if (socket_->setsockopt (ZLINK_INTERNAL_OPT_TLS_TRUST_SYSTEM, &trust_system_,
                             sizeof (trust_system_))
        != 0)
        return -1;
    return 0;
}

static int monitor_event_mask ()
{
    return ZLINK_EVENT_CONNECTION_READY_CHANGED | ZLINK_EVENT_DISCONNECTED
           | ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL
           | ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL
           | ZLINK_EVENT_HANDSHAKE_FAILED_AUTH;
}

static std::string routing_id_key (const zlink_routing_id_t &rid_)
{
    if (rid_.size == 0)
        return std::string ();
    return std::string (reinterpret_cast<const char *> (rid_.data), rid_.size);
}

static bool routing_id_equals (const zlink_routing_id_t &a_,
                               const zlink_routing_id_t &b_)
{
    if (a_.size != b_.size || a_.size == 0)
        return false;
    return memcmp (a_.data, b_.data, a_.size) == 0;
}

static bool pipe_routing_id_equals (const pipe_t *pipe_,
                                    const zlink_routing_id_t *rid_)
{
    if (!pipe_ || !rid_)
        return false;

    const pipe_t *routing_pipe = pipe_;
    if (routing_pipe->get_peer ())
        routing_pipe = routing_pipe->get_peer ();

    const blob_t &routing_id = routing_pipe->get_routing_id ();
    if (routing_id.size () != rid_->size)
        return false;
    if (routing_id.size () == 0)
        return false;
    return memcmp (routing_id.data (), rid_->data, rid_->size) == 0;
}

static void rollback_gateway_runtime_socket_init (gateway_runtime_t *runtime_)
{
    if (!runtime_)
        return;

    if (runtime_->monitor_socket) {
        socket_base_t *monitor_socket =
          static_cast<socket_base_t *> (runtime_->monitor_socket);
        close_socket_monitor_bridge (runtime_->router_socket, monitor_socket);
        (void) runtime_->lifecycle.close_socket_and_wait (monitor_socket, 2000);
        runtime_->monitor_socket = NULL;
    }
    if (runtime_->router_socket)
        (void) runtime_->lifecycle.close_socket_and_wait (runtime_->router_socket,
                                                         2000);
    (void) runtime_->lifecycle.wait_drained (2000);
}

static int send_parts_via_dispatch_pipe (pipe_t *pipe_,
                                         zlink_msg_t *parts_,
                                         size_t part_count_)
{
    if (!pipe_ || !parts_ || part_count_ == 0) {
        errno = EINVAL;
        return -1;
    }

    pipe_t *out = pipe_->get_peer ();
    if (!out) {
        errno = EHOSTUNREACH;
        return -1;
    }

    for (size_t i = 0; i < part_count_; ++i) {
        msg_t *part = reinterpret_cast<msg_t *> (&parts_[i]);
        const bool has_more = i + 1 < part_count_;
        if (has_more)
            part->set_flags (msg_t::more);
        else
            part->reset_flags (msg_t::more);
        if (!out->write_no_hwm_check (part)) {
            if (i > 0)
                out->rollback ();
            errno = EAGAIN;
            return -1;
        }
        const int init_rc = part->init ();
        errno_assert (init_rc == 0);
    }

    out->flush ();
    return 0;
}

static uint64_t rid_handover_guard_ms ()
{
    return 50;
}

static std::string gateway_peer_key (const std::string &service_name_,
                                     const zlink_routing_id_t &rid_)
{
    if (service_name_.empty () || rid_.size == 0)
        return std::string ();

    std::string key = service_name_;
    key.push_back ('\0');
    key.append (reinterpret_cast<const char *> (rid_.data), rid_.size);
    return key;
}

}
