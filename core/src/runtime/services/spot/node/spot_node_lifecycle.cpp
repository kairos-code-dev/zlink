/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/node/spot_node.hpp"
#include "services/spot/runtime/spot_handle.hpp"
#include "services/spot/common/spot_control_protocol.hpp"
#include "services/spot/common/spot_debug.hpp"
#include "services/spot/pubsub/spot_pub.hpp"
#include "services/spot/runtime/spot_runtime.hpp"
#include "services/spot/pubsub/spot_sub.hpp"
#include "services/spot/node/spot_node_router_channel_arg.hpp"

#include "services/common/monitor_decode.hpp"
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

int spot_node_t::ensure_registered ()
{
    if (ensure_healthy () != 0)
        return -1;

    discovery_t *discovery = NULL;
    std::map<std::string, discovery_t *> service_discoveries;
    std::string advertise;
    std::string router_advertise;
    bool has_pubsub = false;
    zlink_routing_id_t node_rid;
    memset (&node_rid, 0, sizeof (node_rid));
    {
        scoped_lock_t lock (_sync);
        discovery = _discovery_state.discovery;
        if (_discovery_state.registered)
            return 0;
        has_pubsub = pubsub_enabled ();
        router_advertise = _endpoint_state.router_bind_endpoint;
        if (_endpoint_state.bound_endpoint.empty () || (!has_pubsub && router_advertise.empty ())) {
            errno = EFSM;
            return -1;
        }
        advertise = _discovery_state.advertise_endpoint.empty ()
                      ? _endpoint_state.bound_endpoint
                      : _discovery_state.advertise_endpoint;
    }
    if (!discovery) {
        errno = EFSM;
        return -1;
    }
    if (has_pubsub && !validate_public_endpoint (advertise)) {
        errno = EINVAL;
        return -1;
    }
    if (!router_advertise.empty () && !validate_public_endpoint (router_advertise)) {
        errno = EINVAL;
        return -1;
    }

    if (node_routing_id (&node_rid) != 0 || node_rid.size == 0) {
        return -1;
    }

    std::string resolved;
    if (has_pubsub) {
        if (discovery_owned_service::register_endpoint (discovery, advertise.c_str (), &resolved,
                                                        &node_rid,
                                                        discovery_protocol::service_role_spot, 100)
            != 0) {
            return -1;
        }
    }

    std::string resolved_router;
    if (!router_advertise.empty ()) {
        if (discovery_owned_service::register_endpoint (
              discovery, router_advertise.c_str (), &resolved_router, &node_rid,
              discovery_protocol::service_role_router, 100)
            != 0) {
            if (has_pubsub) {
                (void) discovery_owned_service::unregister_endpoint (
                  discovery, resolved.empty () ? advertise.c_str () : resolved.c_str ());
            }
            return -1;
        }
    }

    {
        scoped_lock_t lock (_sync);
        _discovery_state.registered = true;
        if (has_pubsub)
            _discovery_state.advertise_endpoint = resolved.empty () ? advertise : resolved;
        else
            _discovery_state.advertise_endpoint =
              resolved_router.empty () ? router_advertise : resolved_router;
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
    bool has_pubsub = false;
    {
        scoped_lock_t lock (_sync);
        if (!_discovery_state.registered) {
            return 0;
        }
        advertise = _discovery_state.advertise_endpoint;
        has_pubsub = pubsub_enabled ();
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
    std::string router_advertise;
    {
        scoped_lock_t lock (_sync);
        router_advertise = _endpoint_state.router_bind_endpoint;
    }
    if (!router_advertise.empty ()) {
        (void) discovery_owned_service::unregister_endpoint (
          discovery, router_advertise.c_str (), discovery_protocol::service_role_router);
    }
    if (has_pubsub
        && discovery_owned_service::unregister_endpoint (discovery, advertise.c_str (),
                                                         discovery_protocol::service_role_spot)
             != 0)
        return -1;

    scoped_lock_t lock (_sync);
    _discovery_state.registered = false;
    _discovery_state.advertise_endpoint.clear ();
    _discovery_state.registration_uplink_endpoint.clear ();
    return 0;
}

void spot_node_service_attachments_t::register_monitor_locked (
  socket_base_t *owner_socket_, void *monitor_handle_, const std::string &channel_name_)
{
    spot_node_attachment_monitor_handle_t monitor_entry;
    monitor_entry.handle = monitor_handle_;
    monitor_entry.owner_socket = owner_socket_;
    monitor_entry.channel_name = channel_name_;
    _state.monitors.push_back (monitor_entry);
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
            (void) discovery_access_t::remove_observer (_discovery_state.discovery, this);
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
    std::string router_advertise;
    {
        scoped_lock_t lock (_sync);
        router_advertise = _endpoint_state.router_bind_endpoint;
    }
    if (!router_advertise.empty () && !validate_public_endpoint (router_advertise)) {
        errno = EINVAL;
        return -1;
    }
    zlink_routing_id_t node_rid;
    memset (&node_rid, 0, sizeof (node_rid));
    if (!router_advertise.empty ()
        && (node_routing_id (&node_rid) != 0 || node_rid.size == 0)) {
        return -1;
    }
    {
        scoped_lock_t lock (_sync);
        spot_node_router_channel_peer_state_t &state =
          service_attachments ().router_channel_peers[channel_name];
        if (state.discovery || !state.manual_endpoints.empty ()
            || !state.active_endpoints.empty ()) {
            errno = EBUSY;
            return -1;
        }
        state.discovery = discovery_;
        service_attachments ().pending_router_channel_refreshes.insert (channel_name);
        _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
    }
    if (discovery_access_t::add_observer (discovery_, this) != 0) {
        scoped_lock_t lock (_sync);
        spot_node_router_channel_peer_state_t &state =
          service_attachments ().router_channel_peers[channel_name];
        if (state.discovery == discovery_)
            state.discovery = NULL;
        service_attachments ().pending_router_channel_refreshes.erase (channel_name);
        return -1;
    }
    std::string resolved_router;
    if (!router_advertise.empty ()
        && discovery_owned_service::register_endpoint (
             discovery_, router_advertise.c_str (), &resolved_router, &node_rid,
             discovery_protocol::service_role_router, 100)
             != 0) {
        const int saved_errno = errno;
        (void) discovery_access_t::remove_observer (discovery_, this);
        scoped_lock_t lock (_sync);
        spot_node_router_channel_peer_state_t &state =
          service_attachments ().router_channel_peers[channel_name];
        if (state.discovery == discovery_)
            state.discovery = NULL;
        service_attachments ().pending_router_channel_refreshes.erase (channel_name);
        errno = saved_errno;
        return -1;
    }
    wake_control_task ();
    lock_entry_spot_rid ();
    return 0;
}

bool spot_node_t::actor_route_sync_enabled () const
{
    scoped_lock_t lock (_sync);
    return _discovery_state.discovery && _discovery_state.discovery->actor_route_sync_enabled ();
}

int spot_node_t::bind_actor_route (const char *actor_id_, const void *value_, size_t value_size_)
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
    return discovery_access_t::bind_route (discovery, ZLINK_ROUTE_KIND_ACTOR, actor_id_,
                                           strlen (actor_id_), value_, value_size_);
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
    return discovery_access_t::unbind_route (discovery, ZLINK_ROUTE_KIND_ACTOR, actor_id_,
                                             strlen (actor_id_));
}

bool spot_node_t::spot_owner_route_synced () const
{
    scoped_lock_t lock (_sync);
    return _discovery_state.discovery && _discovery_state.registered
           && _discovery_state.discovery->spot_owner_sync_enabled ();
}

int spot_node_t::attach_channel_dealer (discovery_t *discovery_, socket_base_t *dealer_)
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
        if (service_attachments ().channel_dealer_discoveries.count (channel_name) != 0
            || (service_attachments ().attachments.count (channel_name) != 0
                && !service_attachments ().attachments[channel_name].manual.routers.empty ())) {
            errno = EBUSY;
            return -1;
        }
        if (service_attachments ().socket_index.count (dealer_) != 0) {
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
    if (service_attachments ().channel_dealer_discoveries.count (channel_name) != 0
        || (service_attachments ().attachments.count (channel_name) != 0
            && !service_attachments ().attachments[channel_name].manual.routers.empty ())
        || service_attachments ().socket_index.count (dealer_) != 0) {
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

    service_attachments ().channel_dealer_discoveries[channel_name] = discovery_;
    service_attachment_t &attachment = service_attachments ().attachments[channel_name];
    attachment.manual.routers.push_back (dealer_);
    attachment.manual.channel_dealer_discovery = discovery_;
    service_attachments ().socket_index[dealer_] = channel_name;
    dealer_->lock_channel_name_metadata ();
    _service_attachments.register_monitor_locked (dealer_, monitor, channel_name);
    _service_attachments.rebuild_caches_locked ();
    _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
    _handle_state.entry_spot_rid_locked = true;
    if (_handle_state.entry_spot)
        _handle_state.entry_spot->rid_locked = true;
    return 0;
}

int spot_node_t::attach_channel_dealer_manual (const char *channel_name_, socket_base_t *dealer_)
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
    if (service_attachments ().channel_dealer_discoveries.count (channel_name) != 0
        || (service_attachments ().attachments.count (channel_name) != 0
            && !service_attachments ().attachments[channel_name].manual.routers.empty ())
        || service_attachments ().socket_index.count (dealer_) != 0) {
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

    service_attachment_t &attachment = service_attachments ().attachments[channel_name];
    attachment.manual.routers.push_back (dealer_);
    attachment.manual.channel_dealer_discovery = NULL;
    service_attachments ().socket_index[dealer_] = channel_name;
    dealer_->lock_channel_name_metadata ();
    _service_attachments.register_monitor_locked (dealer_, monitor, channel_name);
    _service_attachments.rebuild_caches_locked ();
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
        if (!_runtime || _runtime->pub_ingress_endpoint.empty () || !_runtime->pub_ingress_sub) {
            errno = EFAULT;
            return -1;
        }
        if (service_attachments ().pub_ingress
            || service_attachments ().socket_index.count (pub_) != 0) {
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
        if (pub_->socket_has_attached_pipes () && ingress_sub->socket_has_attached_pipes ())
            break;
        zlink::sleep_ms (1);
    }

    scoped_lock_t lock (_sync);
    if (service_attachments ().pub_ingress
        || service_attachments ().socket_index.count (pub_) != 0) {
        (void) pub_->term_endpoint (endpoint.c_str ());
        zlink_monitor_close (&monitor);
        errno = EBUSY;
        return -1;
    }

    service_attachments ().pub_ingress = pub_;
    service_attachments ().socket_index[pub_] = "__spot_service_attachment_state.pub_ingress__";
    _service_attachments.register_monitor_locked (
      pub_, monitor, "__spot_service_attachment_state.pub_ingress__");
    _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
    _handle_state.entry_spot_rid_locked = true;
    if (_handle_state.entry_spot)
        _handle_state.entry_spot->rid_locked = true;
    return 0;
}

void spot_node_t::on_service_update (const std::string &channel_name_)
{
    bool should_wake = false;
    {
        scoped_lock_t lock (_sync);
        if (service_attachments ().discoveries.count (channel_name_) != 0) {
            _service_attachments.queue_discovery_refresh_locked (channel_name_);
            _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
            should_wake = true;
        }
        std::map<std::string, spot_node_router_channel_peer_state_t>::iterator rit =
          service_attachments ().router_channel_peers.find (channel_name_);
        if (rit != service_attachments ().router_channel_peers.end () && rit->second.discovery) {
            service_attachments ().pending_router_channel_refreshes.insert (channel_name_);
            _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
            should_wake = true;
        }
        if (!_discovery_state.discovery_service.empty ()
            && channel_name_ == _discovery_state.discovery_service) {
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
    std::vector<std::pair<std::string, std::string>> router_channel_disconnects;
    {
        scoped_lock_t lock (_sync);
        for (std::map<std::string, spot_node_router_channel_peer_state_t>::iterator it =
               service_attachments ().router_channel_peers.begin ();
             it != service_attachments ().router_channel_peers.end ();) {
            if (it->second.discovery != discovery_) {
                ++it;
                continue;
            }
            for (std::set<std::string>::const_iterator eit = it->second.active_endpoints.begin ();
                 eit != it->second.active_endpoints.end (); ++eit) {
                router_channel_disconnects.push_back (std::make_pair (it->first, *eit));
            }
            it->second.peer_rids_by_endpoint.clear ();
            service_attachments ().pending_router_channel_refreshes.erase (it->first);
            it = service_attachments ().router_channel_peers.erase (it);
            _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
        }
        if (_service_attachments.detach_discovered_service_locked (discovery_,
                                                                   &sockets_to_close)) {
            spot_shutdown_logf_local (false, "step=detach_discovered_service node=%p sockets=%zu",
                                      static_cast<void *> (this), sockets_to_close.size ());
            _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
        }
    }
    for (size_t i = 0; i < router_channel_disconnects.size (); ++i) {
        const std::string arg = spot_node_router_channel_arg::from_endpoint (
          router_channel_disconnects[i].first, router_channel_disconnects[i].second);
        (void) send_data_plane_command (spot_control_protocol::cmd_disconnect_router_channel_peer,
                                        arg.c_str ());
    }
    for (size_t i = 0; i < sockets_to_close.size (); ++i) {
        _ctx->close_socket_and_wait (sockets_to_close[i], 1000);
        untrack_owned_socket (sockets_to_close[i]);
    }
    if (!sockets_to_close.empty ())
        return;
    scoped_lock_t lock (_sync);
    for (std::map<std::string, discovery_t *>::iterator it =
           service_attachments ().channel_dealer_discoveries.begin ();
         it != service_attachments ().channel_dealer_discoveries.end (); ++it) {
        if (it->second != discovery_)
            continue;
        const std::string channel_name = it->first;
        std::map<std::string, service_attachment_t>::iterator attach_it =
          service_attachments ().attachments.find (channel_name);
        if (attach_it != service_attachments ().attachments.end ()) {
            std::vector<socket_base_t *> owners = attach_it->second.manual.routers;
            for (size_t i = 0; i < owners.size (); ++i)
                service_attachments ().socket_index.erase (owners[i]);
            _service_attachments.remove_monitors_by_owner_locked (owners);
            attach_it->second.manual.routers.clear ();
            attach_it->second.manual.channel_dealer_discovery = NULL;
            if (!attach_it->second.has_manual_pubsub ()
                && attach_it->second.discovered.routers.empty ()
                && !attach_it->second.has_auto_pubsub ())
                service_attachments ().attachments.erase (attach_it);
        }
        service_attachments ().channel_dealer_discoveries.erase (it);
        _service_attachments.rebuild_caches_locked ();
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
               service_attachments ().channel_dealer_discoveries.begin ();
             it != service_attachments ().channel_dealer_discoveries.end (); ++it) {
            if (it->second == discovery_) {
                _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
                return;
            }
        }
        for (std::map<std::string, discovery_t *>::iterator it =
               service_attachments ().discoveries.begin ();
             it != service_attachments ().discoveries.end (); ++it) {
            if (it->second == discovery_) {
                _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
                return;
            }
        }
        for (std::map<std::string, spot_node_router_channel_peer_state_t>::iterator it =
               service_attachments ().router_channel_peers.begin ();
             it != service_attachments ().router_channel_peers.end (); ++it) {
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

void spot_node_service_attachments_t::queue_discovery_refresh_locked (
  const std::string &channel_name_)
{
    if (!channel_name_.empty ())
        _state.pending_refresh_services.insert (channel_name_);
}
}
