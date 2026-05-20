/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/node/spot_node.hpp"
#include "services/spot/runtime/spot_handle.hpp"
#include "services/spot/common/spot_control_protocol.hpp"
#include "services/spot/pubsub/spot_pub.hpp"
#include "services/spot/runtime/spot_runtime.hpp"
#include "services/spot/pubsub/spot_sub.hpp"

#include "services/common/monitor_decode.hpp"
#include "services/actor/service_spot_actor_internal.hpp"
#include "services/common/socket_monitor_bridge.hpp"
#include "services/control/service_control_runtime.hpp"
#include "services/discovery/discovery_access.hpp"
#include "services/discovery/discovery_owned_service.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "core/recv_internal.hpp"
#include "sockets/common/socket_base.hpp"
#include "utils/clock.hpp"
#include "utils/routing_id.hpp"
#include "utils/sleep.hpp"

#include "api/spot/dispatch/service_spot_dispatch_surface_internal.hpp"

namespace zlink
{
namespace
{
static bool valid_spot_rid_local (const zlink_routing_id_t &rid_)
{
    return rid_.size > 0 && rid_.size <= sizeof (rid_.data);
}

static std::string spot_rid_key_local (const zlink_routing_id_t &rid_)
{
    if (!valid_spot_rid_local (rid_))
        return std::string ();
    return std::string (reinterpret_cast<const char *> (rid_.data), rid_.size);
}

static bool valid_channel_name_local (const char *channel_name_)
{
    return channel_name_ && channel_name_[0] != '\0';
}

static std::string router_channel_peer_arg_local (
  const std::string &channel_name_, const std::string &endpoint_)
{
    return channel_name_ + "\n" + endpoint_;
}

static bool valid_attached_socket_type_local (socket_base_t *socket_,
                                              int expected_type_)
{
    return socket_ && socket_->check_tag ()
           && socket_->socket_type () == expected_type_;
}

static void *open_attachment_monitor_local (socket_base_t *socket_)
{
    zlink_socket_monitor_open_options_t monitor_options;
    memset (&monitor_options, 0, sizeof (monitor_options));
    monitor_options.events = ZLINK_EVENT_ALL;
    return zlink_socket_monitor_open (socket_, &monitor_options);
}

}

int spot_node_t::bind (const char *endpoint_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

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

    if (send_data_plane_command (spot_control_protocol::cmd_bind_pub,
                                 endpoint_)
        != 0)
        return -1;

    // _endpoint_state.bound_endpoint is set by the data plane handler with the resolved
    // endpoint (supports port 0 / ephemeral port allocation).
    std::vector<spot_pub_t *> pubs;
    bool should_register = false;
    {
        scoped_lock_t lock (_sync);
        _tls_state.server_tls_locked = true;
        _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
        pubs.assign (_handle_state.pubs.begin (), _handle_state.pubs.end ());
        should_register = _discovery_state.discovery != NULL;
    }
    if (should_register && ensure_registered () != 0)
        return -1;
    lock_entry_spot_rid ();
    for (size_t i = 0; i < pubs.size (); ++i)
        submit_pub_summary (pubs[i], ZLINK_TOPOLOGY_STATE_READY, 0);
    return 0;
}

int spot_node_t::connect_peer_pub (const char *peer_pub_endpoint_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!peer_pub_endpoint_ || peer_pub_endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (ensure_healthy () != 0)
        return -1;

    bool need_connect = false;
    bool had_active_peers = false;
    {
        scoped_lock_t lock (_sync);
        if (_discovery_state.discovery) {
            errno = EBUSY;
            return -1;
        }
        had_active_peers = !_peer_state.active_endpoints.empty ();
        if (_peer_state.manual_endpoints.count (peer_pub_endpoint_) != 0)
            return 0;
        _peer_state.manual_endpoints.insert (peer_pub_endpoint_);
        _peer_state.observations[peer_pub_endpoint_].last_changed_ms =
          zlink::clock_t ().now_ms ();
        _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
        if (_peer_state.active_endpoints.count (peer_pub_endpoint_) == 0)
            need_connect = true;
    }

    if (need_connect
        && send_data_plane_command (
             spot_control_protocol::cmd_connect_peer_pub, peer_pub_endpoint_)
             != 0) {
        scoped_lock_t lock (_sync);
        _peer_state.manual_endpoints.erase (peer_pub_endpoint_);
        return -1;
    }

    bool has_active_peers = false;
    {
        scoped_lock_t lock (_sync);
        _tls_state.mesh_client_tls_locked = true;
        if (_peer_state.active_endpoints.insert (peer_pub_endpoint_).second)
            _endpoint_state.active_peer_count.fetch_add (1, std::memory_order_acq_rel);
        _peer_state.observations[peer_pub_endpoint_].last_changed_ms =
          zlink::clock_t ().now_ms ();
        _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
        has_active_peers = !_peer_state.active_endpoints.empty ();
    }
    if (has_active_peers) {
        if (!had_active_peers)
            refresh_sub_peer_summaries (true, false);
    }
    lock_entry_spot_rid ();
    return 0;
}

int spot_node_t::disconnect_peer_pub (const char *peer_pub_endpoint_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!peer_pub_endpoint_ || peer_pub_endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (ensure_healthy () != 0)
        return -1;

    bool need_disconnect = false;
    bool had_active_peers = false;
    bool disconnecting_last_active_peer = false;
    {
        scoped_lock_t lock (_sync);
        if (_discovery_state.discovery) {
            errno = EBUSY;
            return -1;
        }
        had_active_peers = !_peer_state.active_endpoints.empty ();
        _peer_state.manual_endpoints.erase (peer_pub_endpoint_);
        _peer_state.observations[peer_pub_endpoint_].last_changed_ms =
          zlink::clock_t ().now_ms ();
        _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
        if (_peer_state.discovery_endpoints.count (peer_pub_endpoint_) == 0
            && _peer_state.active_endpoints.count (peer_pub_endpoint_) != 0) {
            need_disconnect = true;
            disconnecting_last_active_peer = _peer_state.active_endpoints.size () == 1;
        }
    }

    if (need_disconnect && disconnecting_last_active_peer) {
        std::vector<spot_sub_t *> subs;
        {
            scoped_lock_t lock (_sync);
            subs.assign (_handle_state.subs.begin (), _handle_state.subs.end ());
        }
        for (size_t i = 0; i < subs.size (); ++i)
            subs[i]->send_ready_ack_lost_for_endpoint (peer_pub_endpoint_);
    }

    if (need_disconnect
        && send_data_plane_command (
             spot_control_protocol::cmd_disconnect_peer_pub,
             peer_pub_endpoint_)
             != 0)
        return -1;

    if (need_disconnect) {
        bool has_active_peers = false;
        {
            scoped_lock_t lock (_sync);
            if (_peer_state.active_endpoints.erase (peer_pub_endpoint_) != 0)
                _endpoint_state.active_peer_count.fetch_sub (1, std::memory_order_acq_rel);
            _peer_state.observations[peer_pub_endpoint_].last_changed_ms =
              zlink::clock_t ().now_ms ();
            _peer_state.observations[peer_pub_endpoint_].connected_since_ms = 0;
            _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
            has_active_peers = !_peer_state.active_endpoints.empty ();
        }
        if (had_active_peers && !has_active_peers) {
            std::vector<spot_sub_t *> subs;
            {
                scoped_lock_t lock (_sync);
                _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
                subs.assign (_handle_state.subs.begin (), _handle_state.subs.end ());
                clear_peer_readiness_locked (NULL);
            }
            refresh_sub_peer_summaries (false, true);
            for (size_t i = 0; i < subs.size (); ++i)
                subs[i]->mark_all_subjects_lost (NULL);
        }
    }
    return 0;
}

int spot_node_t::disconnect_peer_pub_rid (
  const zlink_routing_id_t *target_node_rid_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!target_node_rid_ || target_node_rid_->size == 0) {
        errno = EINVAL;
        return -1;
    }
    if (ensure_healthy () != 0)
        return -1;

    const std::string rid_key (
      reinterpret_cast<const char *> (target_node_rid_->data),
      target_node_rid_->size);

    std::vector<std::string> endpoints;
    {
        scoped_lock_t lock (_sync);
        std::map<std::string, std::set<std::string> >::const_iterator it =
          _peer_state.peer_endpoints_by_rid.find (rid_key);
        if (it != _peer_state.peer_endpoints_by_rid.end ())
            endpoints.assign (it->second.begin (), it->second.end ());
    }

    if (endpoints.empty ()) {
        errno = ENOENT;
        return -1;
    }

    bool any_disconnected = false;
    for (size_t i = 0; i < endpoints.size (); ++i) {
        if (send_data_plane_command (
              spot_control_protocol::cmd_disconnect_peer_pub,
              endpoints[i].c_str ())
            == 0) {
            any_disconnected = true;
            scoped_lock_t lock (_sync);
            if (_peer_state.active_endpoints.erase (endpoints[i]) != 0)
                _endpoint_state.active_peer_count.fetch_sub (1, std::memory_order_acq_rel);
            _peer_state.manual_endpoints.erase (endpoints[i]);
            _peer_state.discovery_endpoints.erase (endpoints[i]);
            _peer_state.peer_weight_by_endpoint.erase (endpoints[i]);
            _peer_state.observations[endpoints[i]].last_changed_ms =
              zlink::clock_t ().now_ms ();
            _peer_state.observations[endpoints[i]].connected_since_ms = 0;
            _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
        }
    }

    {
        scoped_lock_t lock (_sync);
        _peer_state.peer_weight_by_rid.erase (rid_key);
        _peer_state.peer_endpoints_by_rid.erase (rid_key);
    }

    if (!any_disconnected) {
        errno = ENOENT;
        return -1;
    }
    return 0;
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
      router_channel_peer_arg_local (channel_name, endpoint);
    if (send_data_plane_command (
          spot_control_protocol::cmd_connect_router_channel_peer, arg.c_str ())
        != 0) {
        const int saved_errno = errno;
        scoped_lock_t lock (_sync);
        std::map<std::string, spot_node_router_channel_peer_state_t>::iterator
          it = _service_attachment_state.router_channel_peers.find (channel_name);
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
          it = _service_attachment_state.router_channel_peers.find (channel_name);
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
      router_channel_peer_arg_local (channel_name, endpoint);
    if (send_data_plane_command (
          spot_control_protocol::cmd_disconnect_router_channel_peer,
          arg.c_str ())
        != 0) {
        return -1;
    }

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

    const std::string peer (
      reinterpret_cast<const char *> (peer_rid_->data), peer_rid_->size);
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
                && memcmp (rid.data, peer_rid_->data, peer_rid_->size) == 0) {
                endpoints.push_back (rid_it->first);
            }
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
          router_channel_peer_arg_local (channel_name, endpoints[i]);
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

int spot_node_t::ensure_registered ()
{
    if (ensure_healthy () != 0)
        return -1;

    discovery_t *discovery = NULL;
    std::map<std::string, discovery_t *> service_discoveries;
    std::string advertise;
    zlink_routing_id_t node_rid;
    memset (&node_rid, 0, sizeof (node_rid));
    {
        scoped_lock_t lock (_sync);
        discovery = _discovery_state.discovery;
        if (_discovery_state.registered)
            return 0;
        if (_endpoint_state.bound_endpoint.empty ()) {
            errno = EFSM;
            return -1;
        }
        advertise = _discovery_state.advertise_endpoint.empty () ? _endpoint_state.bound_endpoint
                                                 : _discovery_state.advertise_endpoint;
    }
    if (!discovery) {
        errno = EFSM;
        return -1;
    }
    if (!validate_public_endpoint (advertise)) {
        errno = EINVAL;
        return -1;
    }

    if (node_routing_id (&node_rid) != 0 || node_rid.size == 0) {
        return -1;
    }

    std::string resolved;
    if (discovery_owned_service::register_endpoint (
          discovery, advertise.c_str (), &resolved, &node_rid,
          discovery_protocol::service_role_spot, 100)
        != 0) {
        return -1;
    }

    {
        scoped_lock_t lock (_sync);
        _discovery_state.registered = true;
        _discovery_state.advertise_endpoint = resolved.empty () ? advertise : resolved;
        if (!discovery->latest_registry_uplink (&_discovery_state.registration_uplink_endpoint))
            _discovery_state.registration_uplink_endpoint.clear ();
        _tls_state.registration_tls_locked = true;
    }

    return 0;
}

int spot_node_t::unregister_registered ()
{
    if (ensure_healthy () != 0)
        return -1;

    std::string advertise;
    {
        scoped_lock_t lock (_sync);
        if (!_discovery_state.registered) {
            return 0;
        }
        advertise = _discovery_state.advertise_endpoint;
    }

    discovery_t *discovery = NULL;
    {
        scoped_lock_t lock (_sync);
        discovery = _discovery_state.discovery;
    }
    if (!discovery) {
        errno = EFSM;
        return -1;
    }
    if (discovery_owned_service::unregister_endpoint (
          discovery, advertise.c_str ())
        != 0)
        return -1;

    scoped_lock_t lock (_sync);
    _discovery_state.registered = false;
    _discovery_state.advertise_endpoint.clear ();
    _discovery_state.registration_uplink_endpoint.clear ();
    return 0;
}

int spot_node_t::validate_socket_service_discovery_attach_locked (
  const std::string &channel_name_, discovery_t *discovery_) const
{
    for (std::map<std::string, discovery_t *>::const_iterator it =
           _service_attachment_state.discoveries.begin ();
         it != _service_attachment_state.discoveries.end (); ++it) {
        if (it->second == discovery_) {
            errno = EBUSY;
            return -1;
        }
    }
    if (_service_attachment_state.discoveries.count (channel_name_) != 0) {
        errno = EBUSY;
        return -1;
    }
    return 0;
}

void spot_node_t::register_attachment_monitor_locked (
  socket_base_t *owner_socket_,
  void *monitor_handle_,
  const std::string &channel_name_)
{
    attachment_monitor_handle_t monitor_entry;
    monitor_entry.handle = monitor_handle_;
    monitor_entry.owner_socket = owner_socket_;
    monitor_entry.channel_name = channel_name_;
    _service_attachment_state.monitors.push_back (monitor_entry);
}

void spot_node_t::reset_spot_discovery_state_locked ()
{
    _discovery_state.discovery = NULL;
    _discovery_state.discovery_service.clear ();
    _discovery_state.discovery_seq = 0;
    _discovery_state.pending_service_updates.clear ();
    _peer_state.discovery_endpoints.clear ();
    _peer_state.connected_endpoints.clear ();
    _peer_state.peer_weight_by_endpoint.clear ();
    _peer_state.peer_weight_by_rid.clear ();
    _peer_state.peer_endpoints_by_rid.clear ();
    _discovery_state.registered = false;
    _discovery_state.advertise_endpoint.clear ();
    _discovery_state.registration_uplink_endpoint.clear ();
}

int spot_node_t::attach_discovery (discovery_t *discovery_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!discovery_) {
        errno = EINVAL;
        return -1;
    }

    if (discovery_->auto_connect_type () != ZLINK_AUTO_CONNECT_SPOT_MESH) {
        errno = ENOTSUP;
        return -1;
    }

    bool should_register = false;
    {
        scoped_lock_t lock (_sync);
        if (_discovery_state.discovery == discovery_) {
            errno = EBUSY;
            return -1;
        }
        if (_discovery_state.discovery || !_peer_state.manual_endpoints.empty ()) {
            errno = EBUSY;
            return -1;
        }
        _discovery_state.discovery = discovery_;
        _discovery_state.discovery_service = discovery_->channel_name ();
        _discovery_state.discovery_seq = 0;
        _discovery_state.pending_service_updates.insert (_discovery_state.discovery_service);
        _peer_state.discovery_endpoints.clear ();
        _peer_state.peer_weight_by_endpoint.clear ();
        _peer_state.peer_weight_by_rid.clear ();
        _peer_state.peer_endpoints_by_rid.clear ();
        _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
        should_register = !_endpoint_state.bound_endpoint.empty ();
    }
    if (discovery_access_t::add_observer (discovery_, this) != 0)
        return -1;
    if (should_register && ensure_registered () != 0) {
        scoped_lock_t lock (_sync);
        if (_discovery_state.discovery == discovery_) {
            (void) discovery_access_t::remove_observer (
              _discovery_state.discovery, this);
            _discovery_state.discovery = NULL;
            _discovery_state.discovery_service.clear ();
        }
        return -1;
    }
    refresh_existing_summaries ();
    wake_control_task ();
    lock_entry_spot_rid ();
    return 0;
}

int spot_node_t::attach_router_channel_discovery (const char *channel_name_,
                                                  discovery_t *discovery_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!valid_channel_name_local (channel_name_) || !discovery_) {
        errno = EINVAL;
        return -1;
    }
    if (!routed_enabled ()) {
        errno = ENOTSUP;
        return -1;
    }
    if (ensure_healthy () != 0)
        return -1;
    if (discovery_->auto_connect_type () != ZLINK_AUTO_CONNECT_ROUTE_MESH
        && discovery_->auto_connect_type () != ZLINK_AUTO_CONNECT_CLIENT_SERVER) {
        errno = ENOTSUP;
        return -1;
    }

    const std::string channel_name (channel_name_);
    if (channel_name != discovery_->channel_name ()) {
        errno = EINVAL;
        return -1;
    }
    {
        scoped_lock_t lock (_sync);
        spot_node_router_channel_peer_state_t &state =
          _service_attachment_state.router_channel_peers[channel_name];
        if (state.discovery || !state.manual_endpoints.empty ()
            || !state.active_endpoints.empty ()) {
            errno = EBUSY;
            return -1;
        }
        state.discovery = discovery_;
        _service_attachment_state.pending_router_channel_refreshes.insert (
          channel_name);
        _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
    }
    if (discovery_access_t::add_observer (discovery_, this) != 0) {
        scoped_lock_t lock (_sync);
        spot_node_router_channel_peer_state_t &state =
          _service_attachment_state.router_channel_peers[channel_name];
        if (state.discovery == discovery_)
            state.discovery = NULL;
        _service_attachment_state.pending_router_channel_refreshes.erase (
          channel_name);
        return -1;
    }
    wake_control_task ();
    lock_entry_spot_rid ();
    return 0;
}

bool spot_node_t::actor_route_sync_enabled () const
{
    scoped_lock_t lock (_sync);
    return _discovery_state.discovery
           && _discovery_state.discovery->actor_route_sync_enabled ();
}

int spot_node_t::bind_actor_route (const char *actor_id_,
                                   const void *value_,
                                   size_t value_size_)
{
    if (!valid_channel_name_local (actor_id_) || !value_) {
        errno = EINVAL;
        return -1;
    }

    discovery_t *discovery = NULL;
    {
        scoped_lock_t lock (_sync);
        discovery = _discovery_state.discovery;
    }
    if (!discovery) {
        errno = EAGAIN;
        return -1;
    }
    return discovery_access_t::bind_route (
      discovery, ZLINK_ROUTE_KIND_ACTOR, actor_id_, strlen (actor_id_),
      value_, value_size_);
}

int spot_node_t::unbind_actor_route (const char *actor_id_)
{
    if (!valid_channel_name_local (actor_id_)) {
        errno = EINVAL;
        return -1;
    }

    discovery_t *discovery = NULL;
    {
        scoped_lock_t lock (_sync);
        discovery = _discovery_state.discovery;
    }
    if (!discovery) {
        errno = EAGAIN;
        return -1;
    }
    return discovery_access_t::unbind_route (
      discovery, ZLINK_ROUTE_KIND_ACTOR, actor_id_, strlen (actor_id_));
}

bool spot_node_t::spot_owner_route_synced () const
{
    scoped_lock_t lock (_sync);
    return _discovery_state.discovery && _discovery_state.registered
           && _discovery_state.discovery->spot_owner_sync_enabled ();
}

int spot_node_t::attach_channel_dealer (discovery_t *discovery_,
                                        socket_base_t *dealer_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!discovery_ || !dealer_) {
        errno = EINVAL;
        return -1;
    }
    if (!valid_attached_socket_type_local (dealer_, ZLINK_CORE_SOCKET_DEALER)) {
        errno = EINVAL;
        return -1;
    }
    if (discovery_->auto_connect_type () != ZLINK_AUTO_CONNECT_CLIENT_SERVER
        && discovery_->auto_connect_type () != ZLINK_AUTO_CONNECT_DEALER_MESH) {
        errno = ENOTSUP;
        return -1;
    }
    if (ensure_healthy () != 0)
        return -1;

    const std::string channel_name = discovery_->channel_name ();
    {
        scoped_lock_t lock (_sync);
        if (channel_name.empty ()) {
            errno = EINVAL;
            return -1;
        }
        if (_service_attachment_state.channel_dealer_discoveries.count (channel_name) != 0
            || (_service_attachment_state.attachments.count (channel_name) != 0
                && !_service_attachment_state.attachments[channel_name].manual.routers.empty ())) {
            errno = EBUSY;
            return -1;
        }
        if (_service_attachment_state.socket_index.count (dealer_) != 0) {
            errno = EBUSY;
            return -1;
        }
    }

    if (discovery_access_t::add_observer (discovery_, this) != 0)
        return -1;

    void *monitor = open_attachment_monitor_local (dealer_);
    if (!monitor) {
        (void) discovery_access_t::remove_observer (discovery_, this);
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_service_attachment_state.channel_dealer_discoveries.count (channel_name) != 0
        || (_service_attachment_state.attachments.count (channel_name) != 0
            && !_service_attachment_state.attachments[channel_name].manual.routers.empty ())
        || _service_attachment_state.socket_index.count (dealer_) != 0) {
        zlink_monitor_close (&monitor);
        (void) discovery_access_t::remove_observer (discovery_, this);
        errno = EBUSY;
        return -1;
    }
    if (dealer_->ensure_channel_name_metadata (channel_name.c_str ()) != 0) {
        const int saved_errno = errno;
        zlink_monitor_close (&monitor);
        (void) discovery_access_t::remove_observer (discovery_, this);
        errno = saved_errno;
        return -1;
    }

    _service_attachment_state.channel_dealer_discoveries[channel_name] = discovery_;
    service_attachment_t &attachment = _service_attachment_state.attachments[channel_name];
    attachment.manual.routers.push_back (dealer_);
    attachment.manual.channel_dealer_discovery = discovery_;
    _service_attachment_state.socket_index[dealer_] = channel_name;
    dealer_->lock_channel_name_metadata ();
    register_attachment_monitor_locked (dealer_, monitor, channel_name);
    rebuild_service_attachment_caches_locked ();
    _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
    _handle_state.entry_spot_rid_locked = true;
    if (_handle_state.entry_spot)
        _handle_state.entry_spot->rid_locked = true;
    return 0;
}

int spot_node_t::attach_channel_dealer_manual (const char *channel_name_,
                                               socket_base_t *dealer_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!valid_channel_name_local (channel_name_)
        || !valid_attached_socket_type_local (dealer_, ZLINK_CORE_SOCKET_DEALER)) {
        errno = EINVAL;
        return -1;
    }
    if (ensure_healthy () != 0)
        return -1;

    void *monitor = open_attachment_monitor_local (dealer_);
    if (!monitor)
        return -1;

    scoped_lock_t lock (_sync);
    const std::string channel_name (channel_name_);
    if (_service_attachment_state.channel_dealer_discoveries.count (channel_name) != 0
        || (_service_attachment_state.attachments.count (channel_name) != 0
            && !_service_attachment_state.attachments[channel_name].manual.routers.empty ())
        || _service_attachment_state.socket_index.count (dealer_) != 0) {
        zlink_monitor_close (&monitor);
        errno = EBUSY;
        return -1;
    }
    if (dealer_->ensure_channel_name_metadata (channel_name.c_str ()) != 0) {
        const int saved_errno = errno;
        zlink_monitor_close (&monitor);
        errno = saved_errno;
        return -1;
    }

    service_attachment_t &attachment = _service_attachment_state.attachments[channel_name];
    attachment.manual.routers.push_back (dealer_);
    attachment.manual.channel_dealer_discovery = NULL;
    _service_attachment_state.socket_index[dealer_] = channel_name;
    dealer_->lock_channel_name_metadata ();
    register_attachment_monitor_locked (dealer_, monitor, channel_name);
    rebuild_service_attachment_caches_locked ();
    _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
    _handle_state.entry_spot_rid_locked = true;
    if (_handle_state.entry_spot)
        _handle_state.entry_spot->rid_locked = true;
    return 0;
}

int spot_node_t::attach_pub_ingress (socket_base_t *pub_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!valid_attached_socket_type_local (pub_, ZLINK_CORE_SOCKET_PUB)) {
        errno = EINVAL;
        return -1;
    }
    if (!pubsub_enabled ()) {
        errno = ENOTSUP;
        return -1;
    }
    if (ensure_healthy () != 0)
        return -1;

    std::string endpoint;
    socket_base_t *ingress_sub = NULL;
    {
        scoped_lock_t lock (_sync);
        if (!_runtime || _runtime->pub_ingress_endpoint.empty ()
            || !_runtime->pub_ingress_sub) {
            errno = EFAULT;
            return -1;
        }
        if (_service_attachment_state.pub_ingress
            || _service_attachment_state.socket_index.count (pub_) != 0) {
            errno = EBUSY;
            return -1;
        }
        endpoint = _runtime->pub_ingress_endpoint;
        ingress_sub = _runtime->pub_ingress_sub;
    }

    void *monitor = open_attachment_monitor_local (pub_);
    if (!monitor)
        return -1;

    if (pub_->connect (endpoint.c_str ()) != 0) {
        const int saved_errno = errno;
        zlink_monitor_close (&monitor);
        errno = saved_errno;
        return -1;
    }

    const uint64_t deadline_ms = zlink::clock_t ().now_ms () + 250;
    while (zlink::clock_t ().now_ms () < deadline_ms) {
        if (pub_->socket_has_attached_pipes ()
            && ingress_sub->socket_has_attached_pipes ())
            break;
        zlink::sleep_ms (1);
    }

    scoped_lock_t lock (_sync);
    if (_service_attachment_state.pub_ingress
        || _service_attachment_state.socket_index.count (pub_) != 0) {
        (void) pub_->term_endpoint (endpoint.c_str ());
        zlink_monitor_close (&monitor);
        errno = EBUSY;
        return -1;
    }

    _service_attachment_state.pub_ingress = pub_;
    _service_attachment_state.socket_index[pub_] =
      "__spot_service_attachment_state.pub_ingress__";
    register_attachment_monitor_locked (
      pub_, monitor, "__spot_service_attachment_state.pub_ingress__");
    _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
    _handle_state.entry_spot_rid_locked = true;
    if (_handle_state.entry_spot)
        _handle_state.entry_spot->rid_locked = true;
    return 0;
}

int spot_node_t::try_register_spot_facade (spot_handle_t *spot_)
{
    if (!spot_) {
        errno = EFAULT;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_handle_state.facades.insert (spot_).second)
        return 0;

    errno = EBUSY;
    return -1;
}

void spot_node_t::unregister_spot_facade (spot_handle_t *spot_)
{
    if (!spot_)
        return;

    std::shared_ptr<spot_logical_state_t> removed_state;
    if (spot_->logical_state) {
        scoped_lock_t pubsub_lock (spot_->logical_state->pubsub_sync);
        if (spot_->logical_state->send_ready_subject.load (
              std::memory_order_acquire)
            == spot_) {
            spot_->logical_state->send_ready_handler.store (
              NULL, std::memory_order_release);
            spot_->logical_state->send_ready_subject.store (
              NULL, std::memory_order_release);
            spot_->logical_state->send_ready_userdata.store (
              NULL, std::memory_order_release);
        }
    }
    {
        scoped_lock_t lock (_sync);
        _handle_state.facades.erase (spot_);
        if (!spot_->logical_state || spot_->logical_state->entry)
            return;
        for (std::set<spot_handle_t *>::const_iterator it =
               _handle_state.facades.begin ();
             it != _handle_state.facades.end (); ++it) {
            if ((*it)->logical_state == spot_->logical_state)
                return;
        }
        removed_state = spot_->logical_state;
        _handle_state.spots_by_rid.erase (
          spot_rid_key_local (removed_state->routing_id));
    }
    submit_spot_owner_summary (removed_state, ZLINK_TOPOLOGY_STATE_STOPPED, 0);
}

bool spot_node_t::is_last_spot_facade_for_logical_state (spot_handle_t *spot_)
{
    if (!spot_ || !spot_->logical_state)
        return true;

    scoped_lock_t lock (_sync);
    for (std::set<spot_handle_t *>::const_iterator it =
           _handle_state.facades.begin ();
         it != _handle_state.facades.end (); ++it) {
        if (*it != spot_ && (*it)->logical_state == spot_->logical_state)
            return false;
    }
    return true;
}

std::shared_ptr<spot_logical_state_t> spot_node_t::create_user_spot_state ()
{
    bool publish_summary = false;
    std::shared_ptr<spot_logical_state_t> state;
    {
        scoped_lock_t lock (_sync);
        state = create_logical_spot_state_locked (false);
        if (!state)
            return std::shared_ptr<spot_logical_state_t> ();
        publish_summary = spot_owner_summary_publishable_locked ();
    }
    if (publish_summary)
        submit_spot_owner_summary (state, ZLINK_TOPOLOGY_STATE_READY, 0);
    return state;
}

std::shared_ptr<spot_logical_state_t> spot_node_t::entry_spot_state ()
{
    bool publish_summary = false;
    std::shared_ptr<spot_logical_state_t> state;
    {
        scoped_lock_t lock (_sync);
        if (_handle_state.entry_spot)
            return _handle_state.entry_spot;

        state = create_logical_spot_state_locked (true);
        if (!state)
            return std::shared_ptr<spot_logical_state_t> ();
        publish_summary = spot_owner_summary_publishable_locked ();
    }
    if (publish_summary)
        submit_spot_owner_summary (state, ZLINK_TOPOLOGY_STATE_READY, 0);
    return state;
}

std::shared_ptr<spot_logical_state_t>
spot_node_t::create_logical_spot_state_locked (
  bool entry_, const zlink_routing_id_t *spot_rid_, bool publish_)
{
    std::shared_ptr<spot_logical_state_t> state (
      new (std::nothrow) spot_logical_state_t ());
    if (!state) {
        errno = ENOMEM;
        return std::shared_ptr<spot_logical_state_t> ();
    }
    state->node = this;
    state->stable_id = _handle_state.next_spot_stable_id++;
    if (spot_rid_)
        state->routing_id = *spot_rid_;
    else
        generate_random_uuid_routing_id (&state->routing_id);
    state->entry = entry_;
    state->rid_locked = entry_ && _handle_state.entry_spot_rid_locked;

    const std::string key = spot_rid_key_local (state->routing_id);
    if (key.empty ()) {
        errno = EFAULT;
        return std::shared_ptr<spot_logical_state_t> ();
    }
    if (publish_) {
        if (entry_)
            _handle_state.entry_spot = state;
        _handle_state.spots_by_rid[key] = state;
    }
    return state;
}

bool spot_node_t::spot_owner_summary_publishable_locked () const
{
    return !_discovery_state.discovery_service.empty ()
           && !_endpoint_state.bound_endpoint.empty ();
}

void spot_node_t::lock_entry_spot_rid ()
{
    scoped_lock_t lock (_sync);
    _handle_state.entry_spot_rid_locked = true;
    if (_handle_state.entry_spot)
        _handle_state.entry_spot->rid_locked = true;
}

std::shared_ptr<spot_logical_state_t> spot_node_t::lookup_spot_state (
  const zlink_routing_id_t *spot_rid_)
{
    if (!spot_rid_ || !valid_spot_rid_local (*spot_rid_)) {
        errno = EINVAL;
        return std::shared_ptr<spot_logical_state_t> ();
    }
    scoped_lock_t lock (_sync);
    const std::map<std::string, std::shared_ptr<spot_logical_state_t> >::iterator
      it = _handle_state.spots_by_rid.find (spot_rid_key_local (*spot_rid_));
    if (it == _handle_state.spots_by_rid.end ()) {
        errno = ENOENT;
        return std::shared_ptr<spot_logical_state_t> ();
    }
    return it->second;
}

std::shared_ptr<spot_logical_state_t> spot_node_t::get_or_new_spot_state (
  const zlink_routing_id_t *spot_rid_, bool *created_out_)
{
    if (created_out_)
        *created_out_ = false;
    if (!spot_rid_ || !valid_spot_rid_local (*spot_rid_)) {
        errno = EINVAL;
        return std::shared_ptr<spot_logical_state_t> ();
    }

    std::shared_ptr<spot_logical_state_t> state;
    const std::string key = spot_rid_key_local (*spot_rid_);
    std::unique_lock<mutex_t> lock (_sync);
    for (;;) {
        const std::map<std::string, std::shared_ptr<spot_logical_state_t> >::iterator
          it = _handle_state.spots_by_rid.find (key);
        if (it != _handle_state.spots_by_rid.end ()) {
            state = it->second;
            return state;
        }

        if (_handle_state.pending_spot_creations.insert (key).second) {
            state = create_logical_spot_state_locked (false, spot_rid_, false);
            if (!state) {
                _handle_state.pending_spot_creations.erase (key);
                lock.unlock ();
                _spot_creation_cv.notify_all ();
                return std::shared_ptr<spot_logical_state_t> ();
            }
            if (created_out_)
                *created_out_ = true;
            return state;
        }

        _spot_creation_cv.wait (lock);
    }
}

bool spot_node_t::publish_get_or_new_spot_state (
  const std::shared_ptr<spot_logical_state_t> &state_)
{
    if (!state_)
        return false;

    bool publish_summary = false;
    bool published = false;
    const std::string key = spot_rid_key_local (state_->routing_id);
    {
        scoped_lock_t lock (_sync);
        std::map<std::string, std::shared_ptr<spot_logical_state_t> >::iterator
          it = _handle_state.spots_by_rid.find (key);
        if (it == _handle_state.spots_by_rid.end ()) {
            _handle_state.spots_by_rid[key] = state_;
            published = true;
            publish_summary = spot_owner_summary_publishable_locked ();
        }
        _handle_state.pending_spot_creations.erase (key);
    }
    _spot_creation_cv.notify_all ();

    if (published && publish_summary)
        submit_spot_owner_summary (state_, ZLINK_TOPOLOGY_STATE_READY, 0);
    return published;
}

void spot_node_t::cancel_get_or_new_spot_state (
  const std::shared_ptr<spot_logical_state_t> &state_)
{
    if (!state_)
        return;

    const std::string key = spot_rid_key_local (state_->routing_id);
    {
        scoped_lock_t lock (_sync);
        _handle_state.pending_spot_creations.erase (key);
    }
    _spot_creation_cv.notify_all ();
}

void spot_node_t::remove_spot_state_if_unfacaded (
  const std::shared_ptr<spot_logical_state_t> &state_)
{
    if (!state_ || state_->entry)
        return;

    bool removed = false;
    {
        scoped_lock_t lock (_sync);
        for (std::set<spot_handle_t *>::const_iterator it =
               _handle_state.facades.begin ();
             it != _handle_state.facades.end (); ++it) {
            if ((*it)->logical_state == state_)
                return;
        }

        const std::string key = spot_rid_key_local (state_->routing_id);
        std::map<std::string, std::shared_ptr<spot_logical_state_t> >::iterator
          it = _handle_state.spots_by_rid.find (key);
        if (it != _handle_state.spots_by_rid.end () && it->second == state_) {
            _handle_state.spots_by_rid.erase (it);
            removed = true;
        }
    }
    if (removed)
        submit_spot_owner_summary (state_, ZLINK_TOPOLOGY_STATE_STOPPED, 0);
}

void spot_node_t::snapshot_spot_states (
  std::vector<std::shared_ptr<spot_logical_state_t> > *out_) const
{
    if (!out_)
        return;

    scoped_lock_t lock (_sync);
    out_->reserve (out_->size () + _handle_state.spots_by_rid.size ());
    for (std::map<std::string, std::shared_ptr<spot_logical_state_t> >::
           const_iterator it = _handle_state.spots_by_rid.begin ();
         it != _handle_state.spots_by_rid.end (); ++it) {
        out_->push_back (it->second);
    }
    std::sort (
      out_->begin (), out_->end (),
      [] (const std::shared_ptr<spot_logical_state_t> &lhs_,
          const std::shared_ptr<spot_logical_state_t> &rhs_) {
          const uint64_t lhs_id = lhs_ ? lhs_->stable_id : 0;
          const uint64_t rhs_id = rhs_ ? rhs_->stable_id : 0;
          return lhs_id < rhs_id;
      });
}

namespace
{
bool spot_logical_topic_matches_local (
  const std::shared_ptr<spot_logical_state_t> &state_,
  const std::string &topic_)
{
    if (!state_ || topic_.empty ())
        return false;

    scoped_lock_t lock (state_->pubsub_sync);
    if (state_->subscription_topics.count (topic_) != 0)
        return true;
    for (std::set<std::string>::const_iterator it =
           state_->subscription_patterns.begin ();
         it != state_->subscription_patterns.end (); ++it) {
        if (topic_.size () >= it->size ()
            && memcmp (topic_.data (), it->data (), it->size ()) == 0) {
            return true;
        }
    }
    return false;
}

int spot_copy_publish_parts_to_block_local (
  zlink_msg_t *parts_,
  size_t part_count_,
  std::vector<std::string> *out_)
{
    if (!out_ || (part_count_ > 0 && !parts_)) {
        errno = EINVAL;
        return -1;
    }

    out_->clear ();
    out_->reserve (part_count_);
    for (size_t i = 0; i < part_count_; ++i) {
        const size_t size = zlink_msg_size (&parts_[i]);
        const char *data =
          static_cast<const char *> (zlink_msg_data (&parts_[i]));
        out_->push_back (std::string (data ? data : "", size));
    }
    return 0;
}
}

int spot_node_t::fanout_local_publish (const zlink_routing_id_t *source_rid_,
                                       const char *topic_id_,
                                       zlink_msg_t *parts_,
                                       size_t part_count_)
{
    if (!topic_id_ || topic_id_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    std::vector<std::shared_ptr<spot_logical_state_t> > states;
    snapshot_spot_states (&states);
    if (states.empty ())
        return 0;

    struct fanout_target_t
    {
        std::shared_ptr<spot_logical_state_t> state;
    };
    std::vector<fanout_target_t> targets;
    const std::string topic (topic_id_);
    for (size_t i = 0; i < states.size (); ++i) {
        if (!spot_logical_topic_matches_local (states[i], topic))
            continue;
        fanout_target_t target;
        target.state = states[i];
        targets.push_back (target);
    }
    if (targets.empty ())
        return 0;

    std::shared_ptr<spot_logical_pubsub_message_t> block (
      new (std::nothrow) spot_logical_pubsub_message_t ());
    if (!block) {
        errno = ENOMEM;
        return -1;
    }

    memset (&block->source_rid, 0, sizeof (block->source_rid));
    if (source_rid_)
        block->source_rid = *source_rid_;
    block->topic_id = topic;
    if (spot_copy_publish_parts_to_block_local (parts_, part_count_,
                                                &block->parts)
        != 0)
        return -1;

    for (size_t i = 0; i < targets.size (); ++i) {
        bool should_signal = false;
        {
            scoped_lock_t lock (targets[i].state->pubsub_sync);
            targets[i].state->subscribe_queue.push_back (block);
            if (!targets[i].state->subscribe_signal_armed) {
                targets[i].state->subscribe_signal_armed = true;
                should_signal = true;
            }
        }
        if (should_signal && targets[i].state->subscribe_signaler.valid ())
            targets[i].state->subscribe_signaler.send ();
        {
            scoped_lock_t lock (_sync);
            for (std::set<spot_handle_t *>::const_iterator it =
                   _handle_state.facades.begin ();
                 it != _handle_state.facades.end (); ++it) {
                if ((*it)->logical_state == targets[i].state) {
                    zlink_spot_notify_dispatch_info (
                      *it,
                      ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE,
                      ZLINK_SPOT_DISPATCH_SUBJECT_SPOT,
                      *it);
                    break;
                }
            }
        }
    }
    return 0;
}

int spot_node_t::update_spot_routing_id (spot_handle_t *spot_,
                                         const void *data_,
                                         size_t size_)
{
    if (!spot_ || spot_->node != this || !spot_->logical_state) {
        errno = EFAULT;
        return -1;
    }
    if (!data_ || size_ == 0 || size_ > sizeof (spot_->spot_routing_id.data)) {
        errno = EINVAL;
        return -1;
    }

    zlink_routing_id_t next;
    memset (&next, 0, sizeof (next));
    next.size = static_cast<uint8_t> (size_);
    memcpy (next.data, data_, size_);

    if (spot_->logical_state->entry
        && spot_actor_internal::node_has_any_actor (this) != 0) {
        errno = EBUSY;
        return -1;
    }
    zlink_routing_id_t old_rid;
    memset (&old_rid, 0, sizeof (old_rid));
    bool publish_summary = false;
    {
        scoped_lock_t lock (_sync);
        if (spot_->logical_state->entry && spot_->logical_state->rid_locked) {
            errno = EBUSY;
            return -1;
        }
        old_rid = spot_->logical_state->routing_id;
        const std::string old_key = spot_rid_key_local (old_rid);
        const std::string new_key = spot_rid_key_local (next);
        std::map<std::string, std::shared_ptr<spot_logical_state_t> >::const_iterator
          existing = _handle_state.spots_by_rid.find (new_key);
        if (existing != _handle_state.spots_by_rid.end ()
            && existing->second != spot_->logical_state) {
            errno = EADDRINUSE;
            return -1;
        }
        if (new_key != old_key
            && _handle_state.pending_spot_creations.count (new_key) != 0) {
            errno = EBUSY;
            return -1;
        }

        if (!old_key.empty ())
            _handle_state.spots_by_rid.erase (old_key);
        spot_->logical_state->routing_id = next;
        _handle_state.spots_by_rid[new_key] = spot_->logical_state;
        for (std::set<spot_handle_t *>::iterator it =
               _handle_state.facades.begin ();
             it != _handle_state.facades.end (); ++it) {
            if ((*it)->logical_state == spot_->logical_state)
                (*it)->spot_routing_id = next;
        }
        publish_summary = !_discovery_state.discovery_service.empty ()
                          && !_endpoint_state.bound_endpoint.empty ();
    }
    if (publish_summary) {
        submit_spot_owner_summary_for_rid (old_rid, ZLINK_TOPOLOGY_STATE_STOPPED,
                                           0);
        submit_spot_owner_summary (spot_->logical_state,
                                   ZLINK_TOPOLOGY_STATE_READY, 0);
    }
    return 0;
}

void spot_node_t::on_service_update (const std::string &channel_name_)
{
    bool should_wake = false;
    {
        scoped_lock_t lock (_sync);
        if (_service_attachment_state.discoveries.count (channel_name_) != 0) {
            queue_service_discovery_refresh_locked (channel_name_);
            _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
            should_wake = true;
        }
        std::map<std::string, spot_node_router_channel_peer_state_t>::iterator
          rit = _service_attachment_state.router_channel_peers.find (
            channel_name_);
        if (rit != _service_attachment_state.router_channel_peers.end ()
            && rit->second.discovery) {
            _service_attachment_state.pending_router_channel_refreshes.insert (
              channel_name_);
            _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
            should_wake = true;
        }
        if (!_discovery_state.discovery_service.empty () && channel_name_ == _discovery_state.discovery_service) {
            _discovery_state.pending_service_updates.insert (channel_name_);
            _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
            should_wake = true;
        }
    }
    if (should_wake)
        wake_control_task ();
}

void spot_node_t::on_discovery_destroyed (discovery_t *discovery_)
{
    std::vector<socket_base_t *> sockets_to_close;
    std::vector<std::pair<std::string, std::string> > router_channel_disconnects;
    {
        scoped_lock_t lock (_sync);
        for (std::map<std::string, spot_node_router_channel_peer_state_t>::iterator
               it = _service_attachment_state.router_channel_peers.begin ();
             it != _service_attachment_state.router_channel_peers.end ();) {
            if (it->second.discovery != discovery_) {
                ++it;
                continue;
            }
            for (std::set<std::string>::const_iterator eit =
                   it->second.active_endpoints.begin ();
                 eit != it->second.active_endpoints.end (); ++eit) {
                router_channel_disconnects.push_back (
                  std::make_pair (it->first, *eit));
            }
            it->second.peer_rids_by_endpoint.clear ();
            _service_attachment_state.pending_router_channel_refreshes.erase (
              it->first);
            it = _service_attachment_state.router_channel_peers.erase (it);
            _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
        }
        if (detach_discovered_service_locked (discovery_, &sockets_to_close))
            _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
    }
    for (size_t i = 0; i < router_channel_disconnects.size (); ++i) {
        const std::string arg = router_channel_peer_arg_local (
          router_channel_disconnects[i].first,
          router_channel_disconnects[i].second);
        (void) send_data_plane_command (
          spot_control_protocol::cmd_disconnect_router_channel_peer,
          arg.c_str ());
    }
    for (size_t i = 0; i < sockets_to_close.size (); ++i)
    {
        _ctx->close_socket_and_wait (sockets_to_close[i], 1000);
        untrack_owned_socket (sockets_to_close[i]);
    }
    if (!sockets_to_close.empty ())
        return;
    scoped_lock_t lock (_sync);
    for (std::map<std::string, discovery_t *>::iterator it =
           _service_attachment_state.channel_dealer_discoveries.begin ();
         it != _service_attachment_state.channel_dealer_discoveries.end (); ++it) {
        if (it->second != discovery_)
            continue;
        const std::string channel_name = it->first;
        std::map<std::string, service_attachment_t>::iterator attach_it =
          _service_attachment_state.attachments.find (channel_name);
        if (attach_it != _service_attachment_state.attachments.end ()) {
            std::vector<socket_base_t *> owners = attach_it->second.manual.routers;
            for (size_t i = 0; i < owners.size (); ++i)
                _service_attachment_state.socket_index.erase (owners[i]);
            remove_attachment_monitors_by_owner_locked (owners);
            attach_it->second.manual.routers.clear ();
            attach_it->second.manual.channel_dealer_discovery = NULL;
            if (!attach_it->second.has_manual_pubsub ()
                && attach_it->second.discovered.routers.empty ()
                && !attach_it->second.has_auto_pubsub ())
                _service_attachment_state.attachments.erase (attach_it);
        }
        _service_attachment_state.channel_dealer_discoveries.erase (it);
        rebuild_service_attachment_caches_locked ();
        _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
        return;
    }
    if (_discovery_state.discovery != discovery_)
        return;
    reset_spot_discovery_state_locked ();
    _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
}

void spot_node_t::on_discovery_shutdown_requested (discovery_t *discovery_)
{
    {
        scoped_lock_t lock (_sync);
        for (std::map<std::string, discovery_t *>::iterator it =
               _service_attachment_state.channel_dealer_discoveries.begin ();
             it != _service_attachment_state.channel_dealer_discoveries.end (); ++it) {
            if (it->second == discovery_) {
                _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
                return;
            }
        }
        for (std::map<std::string, discovery_t *>::iterator it =
               _service_attachment_state.discoveries.begin ();
            it != _service_attachment_state.discoveries.end (); ++it) {
            if (it->second == discovery_) {
                _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
                return;
            }
        }
        for (std::map<std::string, spot_node_router_channel_peer_state_t>::iterator
               it = _service_attachment_state.router_channel_peers.begin ();
             it != _service_attachment_state.router_channel_peers.end (); ++it) {
            if (it->second.discovery == discovery_) {
                _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
                return;
            }
        }
    }
    if (_discovery_state.discovery != discovery_)
        return;
    _public_api.mark_closing ();
    (void) destroy ();
}

int spot_node_t::set_tls_server (const char *cert_, const char *key_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!cert_ || !key_ || cert_[0] == '\0' || key_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_tls_state.server_tls_locked || !_endpoint_state.bound_endpoint.empty ()) {
        errno = EBUSY;
        return -1;
    }
    _tls_state.tls_cert = cert_;
    _tls_state.tls_key = key_;
    return 0;
}

int spot_node_t::set_tls_client (const char *ca_cert_,
                                 const char *hostname_,
                                 int trust_system_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (trust_system_ < 0) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_tls_state.mesh_client_tls_locked || _tls_state.registration_tls_locked) {
        errno = EBUSY;
        return -1;
    }
    _tls_state.tls_ca = ca_cert_ ? ca_cert_ : "";
    _tls_state.tls_hostname = hostname_ ? hostname_ : "";
    _tls_state.tls_trust_system = trust_system_;
    return 0;
}

void spot_node_t::queue_service_discovery_refresh_locked (
  const std::string &channel_name_)
{
    if (!channel_name_.empty ())
        _service_attachment_state.pending_refresh_services.insert (channel_name_);
}
}
