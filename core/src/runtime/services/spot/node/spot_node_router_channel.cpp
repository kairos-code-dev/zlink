/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/node/spot_node.hpp"

#include "services/spot/common/spot_control_protocol.hpp"
#include "services/spot/node/spot_node_router_channel_arg.hpp"
#include "services/spot/runtime/spot_handle.hpp"

#include "utils/clock.hpp"
#include "utils/routing_id.hpp"

namespace zlink
{
namespace
{
static bool valid_channel_name_local (const char *channel_name_)
{
    return channel_name_ && channel_name_[0] != '\0';
}
}

int spot_node_t::connect_router_channel_peer (const char *channel_name_,
                                              const char *endpoint_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!valid_channel_name_local (channel_name_) || !endpoint_
        || endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (!routed_enabled ()) {
        errno = ENOTSUP;
        return -1;
    }
    if (ensure_healthy () != 0)
        return -1;

    const std::string channel_name (channel_name_);
    const std::string endpoint (endpoint_);
    {
        scoped_lock_t lock (_sync);
        spot_node_router_channel_peer_state_t &state =
          _service_attachment_state.router_channel_peers[channel_name];
        if (state.discovery) {
            errno = EBUSY;
            return -1;
        }
        if (state.manual_endpoints.count (endpoint) != 0)
            return 0;
        state.manual_endpoints.insert (endpoint);
    }

    const std::string arg =
      spot_node_router_channel_arg::from_endpoint (channel_name, endpoint);
    if (send_data_plane_command (
          spot_control_protocol::cmd_connect_router_channel_peer, arg.c_str ())
        != 0) {
        const int saved_errno = errno;
        scoped_lock_t lock (_sync);
        std::map<std::string, spot_node_router_channel_peer_state_t>::iterator
          it = _service_attachment_state.router_channel_peers.find (
            channel_name);
        if (it != _service_attachment_state.router_channel_peers.end ()) {
            it->second.manual_endpoints.erase (endpoint);
            if (it->second.manual_endpoints.empty ()
                && it->second.active_endpoints.empty () && !it->second.discovery)
                _service_attachment_state.router_channel_peers.erase (it);
        }
        errno = saved_errno;
        return -1;
    }

    {
        scoped_lock_t lock (_sync);
        _service_attachment_state.router_channel_peers[channel_name]
          .active_endpoints.insert (endpoint);
        _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
        _handle_state.entry_spot_rid_locked = true;
        if (_handle_state.entry_spot)
            _handle_state.entry_spot->rid_locked = true;
    }
    return 0;
}

int spot_node_t::connect_router_channel_peer_rid (
  const char *channel_name_,
  const zlink_routing_id_t *peer_rid_,
  const char *endpoint_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!valid_channel_name_local (channel_name_) || !valid_routing_id (peer_rid_)
        || !endpoint_ || endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (!routed_enabled ()) {
        errno = ENOTSUP;
        return -1;
    }
    if (ensure_healthy () != 0)
        return -1;

    const std::string channel_name (channel_name_);
    const std::string endpoint (endpoint_);
    {
        scoped_lock_t lock (_sync);
        spot_node_router_channel_peer_state_t &state =
          _service_attachment_state.router_channel_peers[channel_name];
        if (state.discovery) {
            errno = EBUSY;
            return -1;
        }
        if (state.manual_endpoints.count (endpoint) != 0)
            return 0;
        state.manual_endpoints.insert (endpoint);
    }

    const std::string arg = spot_node_router_channel_arg::from_routing_id (
      channel_name, *peer_rid_, endpoint);
    if (send_data_plane_command (
          spot_control_protocol::cmd_connect_router_channel_peer_rid,
          arg.c_str ())
        != 0) {
        const int saved_errno = errno;
        scoped_lock_t lock (_sync);
        std::map<std::string, spot_node_router_channel_peer_state_t>::iterator
          it = _service_attachment_state.router_channel_peers.find (
            channel_name);
        if (it != _service_attachment_state.router_channel_peers.end ()) {
            it->second.manual_endpoints.erase (endpoint);
            if (it->second.manual_endpoints.empty ()
                && it->second.active_endpoints.empty () && !it->second.discovery)
                _service_attachment_state.router_channel_peers.erase (it);
        }
        errno = saved_errno;
        return -1;
    }

    {
        scoped_lock_t lock (_sync);
        spot_node_router_channel_peer_state_t &state =
          _service_attachment_state.router_channel_peers[channel_name];
        state.active_endpoints.insert (endpoint);
        state.peer_rids_by_endpoint[endpoint] = *peer_rid_;
        _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
        _handle_state.entry_spot_rid_locked = true;
        if (_handle_state.entry_spot)
            _handle_state.entry_spot->rid_locked = true;
    }
    return 0;
}

int spot_node_t::disconnect_router_channel_peer (const char *channel_name_,
                                                 const char *endpoint_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!valid_channel_name_local (channel_name_) || !endpoint_
        || endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (!routed_enabled ()) {
        errno = ENOTSUP;
        return -1;
    }
    if (ensure_healthy () != 0)
        return -1;

    const std::string channel_name (channel_name_);
    const std::string endpoint (endpoint_);
    {
        scoped_lock_t lock (_sync);
        std::map<std::string, spot_node_router_channel_peer_state_t>::iterator
          it = _service_attachment_state.router_channel_peers.find (
            channel_name);
        if (it == _service_attachment_state.router_channel_peers.end ()
            || it->second.manual_endpoints.count (endpoint) == 0) {
            errno = ENOENT;
            return -1;
        }
        if (it->second.discovery) {
            errno = EBUSY;
            return -1;
        }
    }

    const std::string arg =
      spot_node_router_channel_arg::from_endpoint (channel_name, endpoint);
    if (send_data_plane_command (
          spot_control_protocol::cmd_disconnect_router_channel_peer, arg.c_str ())
        != 0)
        return -1;

    scoped_lock_t lock (_sync);
    std::map<std::string, spot_node_router_channel_peer_state_t>::iterator it =
      _service_attachment_state.router_channel_peers.find (channel_name);
    if (it != _service_attachment_state.router_channel_peers.end ()) {
        it->second.manual_endpoints.erase (endpoint);
        it->second.active_endpoints.erase (endpoint);
        it->second.peer_rids_by_endpoint.erase (endpoint);
        if (it->second.manual_endpoints.empty ()
            && it->second.active_endpoints.empty () && !it->second.discovery)
            _service_attachment_state.router_channel_peers.erase (it);
    }
    _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
    return 0;
}

int spot_node_t::disconnect_router_channel_peer_rid (
  const char *channel_name_, const zlink_routing_id_t *peer_rid_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!valid_channel_name_local (channel_name_) || !peer_rid_
        || peer_rid_->size == 0 || peer_rid_->size > sizeof (peer_rid_->data)) {
        errno = EINVAL;
        return -1;
    }
    if (!routed_enabled ()) {
        errno = ENOTSUP;
        return -1;
    }
    if (ensure_healthy () != 0)
        return -1;

    const std::string peer = zlink::routing_id_key (peer_rid_);
    std::vector<std::string> endpoints;
    {
        scoped_lock_t lock (_sync);
        std::map<std::string, spot_node_router_channel_peer_state_t>::const_iterator
          it = _service_attachment_state.router_channel_peers.find (
            channel_name_);
        if (it == _service_attachment_state.router_channel_peers.end ()) {
            errno = ENOENT;
            return -1;
        }
        for (std::map<std::string, zlink_routing_id_t>::const_iterator rid_it =
               it->second.peer_rids_by_endpoint.begin ();
             rid_it != it->second.peer_rids_by_endpoint.end (); ++rid_it) {
            const zlink_routing_id_t &rid = rid_it->second;
            if (rid.size == peer_rid_->size
                && memcmp (rid.data, peer_rid_->data, peer_rid_->size) == 0)
                endpoints.push_back (rid_it->first);
        }
        if (endpoints.empty () && it->second.manual_endpoints.count (peer) != 0)
            endpoints.push_back (peer);
        if (endpoints.empty ()) {
            errno = ENOENT;
            return -1;
        }
    }

    bool any_disconnected = false;
    const std::string channel_name (channel_name_);
    for (size_t i = 0; i < endpoints.size (); ++i) {
        const std::string arg =
          spot_node_router_channel_arg::from_endpoint (channel_name,
                                                       endpoints[i]);
        if (send_data_plane_command (
              spot_control_protocol::cmd_disconnect_router_channel_peer,
              arg.c_str ())
            == 0) {
            any_disconnected = true;
            scoped_lock_t lock (_sync);
            std::map<std::string, spot_node_router_channel_peer_state_t>::iterator
              it = _service_attachment_state.router_channel_peers.find (
                channel_name);
            if (it != _service_attachment_state.router_channel_peers.end ()) {
                it->second.manual_endpoints.erase (endpoints[i]);
                it->second.active_endpoints.erase (endpoints[i]);
                it->second.peer_rids_by_endpoint.erase (endpoints[i]);
                if (it->second.manual_endpoints.empty ()
                    && it->second.active_endpoints.empty ()
                    && !it->second.discovery)
                    _service_attachment_state.router_channel_peers.erase (it);
            }
            _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
        }
    }

    if (!any_disconnected) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}
}
