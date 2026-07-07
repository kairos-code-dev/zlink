/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/node/spot_node.hpp"
#include "services/spot/runtime/spot_handle.hpp"
#include "services/spot/common/spot_control_protocol.hpp"
#include "services/spot/common/spot_debug.hpp"
#include "services/spot/pubsub/spot_pub.hpp"
#include "services/spot/runtime/spot_runtime.hpp"
#include "services/spot/pubsub/spot_sub.hpp"

#include "services/common/monitor_decode.hpp"
#include "services/common/socket_monitor_bridge.hpp"
#include "services/control/service_control_runtime.hpp"
#include "core/recv_internal.hpp"
#include "sockets/common/socket_base.hpp"
#include "utils/clock.hpp"
#include "utils/routing_id.hpp"
#include "utils/sleep.hpp"

namespace zlink
{
namespace
{
static bool valid_channel_name_local (const char *channel_name_)
{
    return channel_name_ && channel_name_[0] != '\0';
}

static bool valid_attached_socket_type_local (socket_base_t *socket_, int expected_type_)
{
    return socket_ && socket_->check_tag () && socket_->socket_type () == expected_type_;
}

static void *open_attachment_monitor_local (socket_base_t *socket_)
{
    zlink_socket_monitor_open_options_t monitor_options;
    memset (&monitor_options, 0, sizeof (monitor_options));
    monitor_options.events = ZLINK_EVENT_ALL;
    return zlink_socket_monitor_open (socket_, &monitor_options);
}

static void spot_shutdown_logf_local (bool always_, const char *fmt_, ...)
{
    if (!always_ && !spot_debug::shutdown_enabled ())
        return;

    va_list args;
    va_start (args, fmt_);
    debug_vfprintf (always_ ? NULL : "ZLINK_DEBUG_SPOT_SHUTDOWN", "[spot-shutdown] ", fmt_, args);
    va_end (args);
}

}

int spot_node_t::bind_endpoint (const char *endpoint_)
{
    if (!endpoint_ || endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (ensure_healthy () != 0)
        return -1;

    {
        scoped_lock_t lock (_sync);
        if (!_endpoint_state.bound_endpoint.empty ()) {
            errno = EBUSY;
            return -1;
        }
    }

    if (send_data_plane_command (spot_control_protocol::cmd_bind_pub, endpoint_) != 0)
        return -1;

    // _endpoint_state.bound_endpoint is set by the data plane handler with the resolved
    // endpoint (supports port 0 / ephemeral port allocation).
    std::vector<spot_pub_t *> pubs;
    {
        scoped_lock_t lock (_sync);
        _tls_state.server_tls_locked = true;
        _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
        pubs.assign (_handle_state.pubs.begin (), _handle_state.pubs.end ());
    }
    lock_entry_spot_rid ();
    for (size_t i = 0; i < pubs.size (); ++i)
        submit_pub_summary (pubs[i], ZLINK_TOPOLOGY_STATE_READY, 0);
    return 0;
}

int spot_node_t::set_pub_bind (const char *endpoint_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!pubsub_enabled ()) {
        errno = ENOTSUP;
        return -1;
    }
    return bind_endpoint (endpoint_);
}

int spot_node_t::set_router_bind (const char *endpoint_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!routed_enabled ()) {
        errno = ENOTSUP;
        return -1;
    }
    if (!endpoint_ || endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (ensure_healthy () != 0)
        return -1;

    bool should_start = false;
    {
        scoped_lock_t lock (_sync);
        if (!_endpoint_state.bound_endpoint.empty ()) {
            errno = EBUSY;
            return -1;
        }
        _endpoint_state.router_bind_endpoint = endpoint_;
        should_start = !pubsub_enabled ();
    }
    return should_start ? bind_endpoint (endpoint_) : 0;
}

void spot_node_service_attachments_t::register_monitor_locked (socket_base_t *owner_socket_,
                                                               void *monitor_handle_,
                                                               const std::string &channel_name_)
{
    spot_node_attachment_monitor_handle_t monitor_entry;
    monitor_entry.handle = monitor_handle_;
    monitor_entry.owner_socket = owner_socket_;
    monitor_entry.channel_name = channel_name_;
    _state.monitors.push_back (monitor_entry);
}

}
