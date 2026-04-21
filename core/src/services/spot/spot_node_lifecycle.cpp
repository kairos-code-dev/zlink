/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_node.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_runtime.hpp"
#include "services/spot/spot_sub.hpp"

#include "services/control/service_control_runtime.hpp"
#include "services/discovery/discovery_owned_service.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "core/recv_internal.hpp"
#include "sockets/socket_base.hpp"
#include "utils/clock.hpp"
#include "utils/sleep.hpp"

namespace zlink
{
namespace
{
static bool valid_service_name_local (const char *service_name_)
{
    return service_name_ && service_name_[0] != '\0';
}

static bool valid_attached_socket_type_local (socket_base_t *socket_,
                                              int expected_type_)
{
    return socket_ && socket_->check_tag ()
           && socket_->socket_type () == expected_type_;
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
        if (!_bound_endpoint.empty ()) {
            errno = EBUSY;
            return -1;
        }
    }

    if (send_data_plane_command ("bind_pub", endpoint_) != 0)
        return -1;

    // _bound_endpoint is set by the data plane handler with the resolved
    // endpoint (supports port 0 / ephemeral port allocation).
    std::vector<spot_pub_t *> pubs;
    bool should_register = false;
    {
        scoped_lock_t lock (_sync);
        _server_tls_locked = true;
        _summary_last_changed_ms = zlink::clock_t ().now_ms ();
        pubs.assign (_pubs.begin (), _pubs.end ());
        should_register = _discovery != NULL;
    }
    if (should_register && ensure_registered () != 0)
        return -1;
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
        if (_discovery) {
            errno = EBUSY;
            return -1;
        }
        had_active_peers = !_peer_state.active_endpoints.empty ();
        if (_peer_state.manual_endpoints.count (peer_pub_endpoint_) != 0)
            return 0;
        _peer_state.manual_endpoints.insert (peer_pub_endpoint_);
        _peer_state.observations[peer_pub_endpoint_].last_changed_ms =
          zlink::clock_t ().now_ms ();
        _summary_last_changed_ms = zlink::clock_t ().now_ms ();
        if (_peer_state.active_endpoints.count (peer_pub_endpoint_) == 0)
            need_connect = true;
    }

    if (need_connect && send_data_plane_command ("connect_peer_pub",
                                                 peer_pub_endpoint_)
                           != 0) {
        scoped_lock_t lock (_sync);
        _peer_state.manual_endpoints.erase (peer_pub_endpoint_);
        return -1;
    }

    bool has_active_peers = false;
    {
        scoped_lock_t lock (_sync);
        _mesh_client_tls_locked = true;
        if (_peer_state.active_endpoints.insert (peer_pub_endpoint_).second)
            _active_peer_count.fetch_add (1, std::memory_order_acq_rel);
        _peer_state.observations[peer_pub_endpoint_].last_changed_ms =
          zlink::clock_t ().now_ms ();
        _summary_last_changed_ms = zlink::clock_t ().now_ms ();
        has_active_peers = !_peer_state.active_endpoints.empty ();
    }
    if (has_active_peers) {
        if (!had_active_peers)
            refresh_sub_peer_summaries (true, false);
    }
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
        if (_discovery) {
            errno = EBUSY;
            return -1;
        }
        had_active_peers = !_peer_state.active_endpoints.empty ();
        _peer_state.manual_endpoints.erase (peer_pub_endpoint_);
        _peer_state.observations[peer_pub_endpoint_].last_changed_ms =
          zlink::clock_t ().now_ms ();
        _summary_last_changed_ms = zlink::clock_t ().now_ms ();
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
            subs.assign (_subs.begin (), _subs.end ());
        }
        for (size_t i = 0; i < subs.size (); ++i)
            subs[i]->send_ready_ack_lost_for_endpoint (peer_pub_endpoint_);
    }

    if (need_disconnect
        && send_data_plane_command ("disconnect_peer_pub", peer_pub_endpoint_)
             != 0)
        return -1;

    if (need_disconnect) {
        bool has_active_peers = false;
        {
            scoped_lock_t lock (_sync);
            if (_peer_state.active_endpoints.erase (peer_pub_endpoint_) != 0)
                _active_peer_count.fetch_sub (1, std::memory_order_acq_rel);
            _peer_state.observations[peer_pub_endpoint_].last_changed_ms =
              zlink::clock_t ().now_ms ();
            _peer_state.observations[peer_pub_endpoint_].connected_since_ms = 0;
            _summary_last_changed_ms = zlink::clock_t ().now_ms ();
            has_active_peers = !_peer_state.active_endpoints.empty ();
        }
        if (had_active_peers && !has_active_peers) {
            std::vector<spot_sub_t *> subs;
            {
                scoped_lock_t lock (_sync);
                _summary_last_changed_ms = zlink::clock_t ().now_ms ();
                subs.assign (_subs.begin (), _subs.end ());
                clear_peer_readiness_locked (NULL);
            }
            refresh_sub_peer_summaries (false, true);
            for (size_t i = 0; i < subs.size (); ++i)
                subs[i]->mark_all_subjects_lost (NULL);
        }
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
        discovery = _discovery;
        if (_registered)
            return 0;
        if (_bound_endpoint.empty ()) {
            errno = EFSM;
            return -1;
        }
        advertise = _advertise_endpoint.empty () ? _bound_endpoint
                                                 : _advertise_endpoint;
    }
    if (!discovery) {
        errno = EFSM;
        return -1;
    }
    if (!validate_public_endpoint (advertise)) {
        errno = EINVAL;
        return -1;
    }

    spot_pub_t *node_pub = ensure_default_pub ();
    if (!node_pub || node_pub->routing_id (&node_rid) != 0
        || node_rid.size == 0) {
        return -1;
    }

    std::string resolved;
    if (discovery_owned_service::register_endpoint (
          discovery, discovery_protocol::service_type_spot_node,
          advertise.c_str (), &resolved, &node_rid,
          discovery_protocol::service_role_spot, _admission_state)
        != 0) {
        return -1;
    }

    {
        scoped_lock_t lock (_sync);
        _registered = true;
        _advertise_endpoint = resolved.empty () ? advertise : resolved;
        if (!discovery->latest_registry_uplink (&_registration_uplink_endpoint))
            _registration_uplink_endpoint.clear ();
        _registration_tls_locked = true;
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
        if (!_registered) {
            return 0;
        }
        advertise = _advertise_endpoint;
    }

    discovery_t *discovery = NULL;
    {
        scoped_lock_t lock (_sync);
        discovery = _discovery;
    }
    if (!discovery) {
        errno = EFSM;
        return -1;
    }
    if (discovery_owned_service::unregister_endpoint (
          discovery, discovery_protocol::service_type_spot_node,
          advertise.c_str ())
        != 0)
        return -1;

    scoped_lock_t lock (_sync);
    _registered = false;
    _advertise_endpoint.clear ();
    _registration_uplink_endpoint.clear ();
    return 0;
}

int spot_node_t::validate_socket_service_discovery_attach_locked (
  const std::string &service_name_, discovery_t *discovery_) const
{
    for (std::map<std::string, discovery_t *>::const_iterator it =
           _service_discoveries.begin ();
         it != _service_discoveries.end (); ++it) {
        if (it->second == discovery_) {
            errno = EBUSY;
            return -1;
        }
    }
    if (_facades.size () > 1) {
        errno = EBUSY;
        return -1;
    }
    if (_service_discoveries.count (service_name_) != 0) {
        errno = EBUSY;
        return -1;
    }
    return 0;
}

void spot_node_t::register_service_monitor_locked (
  socket_base_t *owner_socket_,
  void *monitor_handle_,
  const std::string &service_name_)
{
    service_monitor_handle_t monitor_entry;
    monitor_entry.handle = monitor_handle_;
    monitor_entry.owner_socket = owner_socket_;
    monitor_entry.service_name = service_name_;
    _service_monitors.push_back (monitor_entry);
}

void spot_node_t::reset_spot_discovery_state_locked ()
{
    _discovery = NULL;
    _discovery_service.clear ();
    _discovery_seq = 0;
    _pending_service_updates.clear ();
    _peer_state.discovery_endpoints.clear ();
    _peer_state.connected_endpoints.clear ();
    _peer_state.peer_admission_by_endpoint.clear ();
    _peer_state.peer_admission_by_rid.clear ();
    _registered = false;
    _advertise_endpoint.clear ();
    _registration_uplink_endpoint.clear ();
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

    if (discovery_->service_type () != discovery_protocol::service_type_spot_node) {
        errno = EINVAL;
        return -1;
    }

    bool should_register = false;
    {
        scoped_lock_t lock (_sync);
        if (_discovery == discovery_) {
            errno = EBUSY;
            return -1;
        }
        if (_facades.size () > 1) {
            errno = EBUSY;
            return -1;
        }
        if (_discovery || !_peer_state.manual_endpoints.empty ()) {
            errno = EBUSY;
            return -1;
        }
        _discovery = discovery_;
        _discovery_service = discovery_->service_name ();
        _discovery_seq = 0;
        _pending_service_updates.insert (_discovery_service);
        _peer_state.discovery_endpoints.clear ();
        _peer_state.peer_admission_by_endpoint.clear ();
        _peer_state.peer_admission_by_rid.clear ();
        _summary_last_changed_ms = zlink::clock_t ().now_ms ();
        should_register = !_bound_endpoint.empty ();
    }
    if (discovery_->add_observer (this) != 0)
        return -1;
    if (should_register && ensure_registered () != 0) {
        scoped_lock_t lock (_sync);
        if (_discovery == discovery_) {
            _discovery->remove_observer (this);
            _discovery = NULL;
            _discovery_service.clear ();
        }
        return -1;
    }
    refresh_existing_summaries ();
    wake_control_task ();
    return 0;
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
    if (discovery_->service_type () != discovery_protocol::service_type_socket
        || !valid_attached_socket_type_local (dealer_, ZLINK_CORE_SOCKET_DEALER)) {
        errno = EINVAL;
        return -1;
    }
    if (ensure_healthy () != 0)
        return -1;

    const std::string channel_name = discovery_->service_name ();
    {
        scoped_lock_t lock (_sync);
        if (channel_name.empty ()) {
            errno = EINVAL;
            return -1;
        }
        if (_channel_dealer_discoveries.count (channel_name) != 0
            || (_service_attachments.count (channel_name) != 0
                && !_service_attachments[channel_name].manual.routers.empty ())) {
            errno = EBUSY;
            return -1;
        }
        if (_service_attachment_socket_index.count (dealer_) != 0) {
            errno = EBUSY;
            return -1;
        }
    }

    if (discovery_->add_observer (this) != 0)
        return -1;

    zlink_socket_monitor_open_options_t monitor_options;
    memset (&monitor_options, 0, sizeof (monitor_options));
    monitor_options.events = ZLINK_EVENT_ALL;
    void *monitor = zlink_socket_monitor_open (dealer_, &monitor_options);
    if (!monitor) {
        discovery_->remove_observer (this);
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_channel_dealer_discoveries.count (channel_name) != 0
        || (_service_attachments.count (channel_name) != 0
            && !_service_attachments[channel_name].manual.routers.empty ())
        || _service_attachment_socket_index.count (dealer_) != 0) {
        zlink_monitor_close (&monitor);
        discovery_->remove_observer (this);
        errno = EBUSY;
        return -1;
    }
    if (dealer_->ensure_channel_name_metadata (channel_name.c_str ()) != 0) {
        const int saved_errno = errno;
        zlink_monitor_close (&monitor);
        discovery_->remove_observer (this);
        errno = saved_errno;
        return -1;
    }

    _channel_dealer_discoveries[channel_name] = discovery_;
    service_attachment_t &attachment = _service_attachments[channel_name];
    attachment.manual.routers.push_back (dealer_);
    attachment.manual.channel_dealer_discovery = discovery_;
    _service_attachment_socket_index[dealer_] = channel_name;
    dealer_->lock_channel_name_metadata ();
    register_service_monitor_locked (dealer_, monitor, channel_name);
    rebuild_service_attachment_caches_locked ();
    _summary_last_changed_ms = zlink::clock_t ().now_ms ();
    return 0;
}

int spot_node_t::attach_channel_dealer_manual (const char *channel_name_,
                                               socket_base_t *dealer_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!valid_service_name_local (channel_name_)
        || !valid_attached_socket_type_local (dealer_, ZLINK_CORE_SOCKET_DEALER)) {
        errno = EINVAL;
        return -1;
    }
    if (ensure_healthy () != 0)
        return -1;

    zlink_socket_monitor_open_options_t monitor_options;
    memset (&monitor_options, 0, sizeof (monitor_options));
    monitor_options.events = ZLINK_EVENT_ALL;
    void *monitor = zlink_socket_monitor_open (dealer_, &monitor_options);
    if (!monitor)
        return -1;

    scoped_lock_t lock (_sync);
    const std::string channel_name (channel_name_);
    if (_channel_dealer_discoveries.count (channel_name) != 0
        || (_service_attachments.count (channel_name) != 0
            && !_service_attachments[channel_name].manual.routers.empty ())
        || _service_attachment_socket_index.count (dealer_) != 0) {
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

    service_attachment_t &attachment = _service_attachments[channel_name];
    attachment.manual.routers.push_back (dealer_);
    attachment.manual.channel_dealer_discovery = NULL;
    _service_attachment_socket_index[dealer_] = channel_name;
    dealer_->lock_channel_name_metadata ();
    register_service_monitor_locked (dealer_, monitor, channel_name);
    rebuild_service_attachment_caches_locked ();
    _summary_last_changed_ms = zlink::clock_t ().now_ms ();
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
    if (ensure_healthy () != 0)
        return -1;

    const std::string endpoint = pub_ingress_endpoint ();
    if (endpoint.empty ()) {
        errno = EFAULT;
        return -1;
    }

    {
        scoped_lock_t lock (_sync);
        if (_pub_ingress || _service_attachment_socket_index.count (pub_) != 0) {
            errno = EBUSY;
            return -1;
        }
    }

    if (pub_->connect (endpoint.c_str ()) != 0)
        return -1;

    scoped_lock_t lock (_sync);
    if (_pub_ingress || _service_attachment_socket_index.count (pub_) != 0) {
        (void) pub_->term_endpoint (endpoint.c_str ());
        errno = EBUSY;
        return -1;
    }
    _pub_ingress = pub_;
    _service_attachment_socket_index[pub_] = std::string ("__spot_pub_ingress__");
    _summary_last_changed_ms = zlink::clock_t ().now_ms ();
    return 0;
}

int spot_node_t::try_register_spot_facade (spot_handle_t *spot_)
{
    if (!spot_) {
        errno = EFAULT;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if ((_discovery != NULL || !_service_discoveries.empty ()
         || !_service_attachments.empty ())
        && !_facades.empty ()) {
        errno = EBUSY;
        return -1;
    }
    if (_facades.insert (spot_).second)
        return 0;

    errno = EBUSY;
    return -1;
}

void spot_node_t::unregister_spot_facade (spot_handle_t *spot_)
{
    if (!spot_)
        return;

    scoped_lock_t lock (_sync);
    _facades.erase (spot_);
}

void spot_node_t::on_service_update (const std::string &service_name_)
{
    bool should_wake = false;
    {
        scoped_lock_t lock (_sync);
        if (_service_discoveries.count (service_name_) != 0) {
            queue_service_discovery_refresh_locked (service_name_);
            _summary_last_changed_ms = zlink::clock_t ().now_ms ();
            should_wake = true;
        }
        if (!_discovery_service.empty () && service_name_ == _discovery_service) {
            _pending_service_updates.insert (service_name_);
            _summary_last_changed_ms = zlink::clock_t ().now_ms ();
            should_wake = true;
        }
    }
    if (should_wake)
        wake_control_task ();
}

void spot_node_t::on_discovery_destroyed (discovery_t *discovery_)
{
    std::vector<socket_base_t *> sockets_to_close;
    {
        scoped_lock_t lock (_sync);
        if (detach_discovered_service_locked (discovery_, &sockets_to_close))
            _summary_last_changed_ms = zlink::clock_t ().now_ms ();
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
           _channel_dealer_discoveries.begin ();
         it != _channel_dealer_discoveries.end (); ++it) {
        if (it->second != discovery_)
            continue;
        const std::string channel_name = it->first;
        std::map<std::string, service_attachment_t>::iterator attach_it =
          _service_attachments.find (channel_name);
        if (attach_it != _service_attachments.end ()) {
            std::vector<socket_base_t *> owners = attach_it->second.manual.routers;
            for (size_t i = 0; i < owners.size (); ++i)
                _service_attachment_socket_index.erase (owners[i]);
            remove_service_monitors_by_owner_locked (owners);
            attach_it->second.manual.routers.clear ();
            attach_it->second.manual.channel_dealer_discovery = NULL;
            if (!attach_it->second.has_manual_pubsub ()
                && attach_it->second.discovered.routers.empty ()
                && !attach_it->second.has_auto_pubsub ())
                _service_attachments.erase (attach_it);
        }
        _channel_dealer_discoveries.erase (it);
        rebuild_service_attachment_caches_locked ();
        _summary_last_changed_ms = zlink::clock_t ().now_ms ();
        return;
    }
    if (_discovery != discovery_)
        return;
    reset_spot_discovery_state_locked ();
    _summary_last_changed_ms = zlink::clock_t ().now_ms ();
}

void spot_node_t::on_discovery_shutdown_requested (discovery_t *discovery_)
{
    {
        scoped_lock_t lock (_sync);
        for (std::map<std::string, discovery_t *>::iterator it =
               _channel_dealer_discoveries.begin ();
             it != _channel_dealer_discoveries.end (); ++it) {
            if (it->second == discovery_) {
                _summary_last_changed_ms = zlink::clock_t ().now_ms ();
                return;
            }
        }
        for (std::map<std::string, discovery_t *>::iterator it =
               _service_discoveries.begin ();
            it != _service_discoveries.end (); ++it) {
            if (it->second == discovery_) {
                _summary_last_changed_ms = zlink::clock_t ().now_ms ();
                return;
            }
        }
    }
    if (_discovery != discovery_)
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
    if (_server_tls_locked || !_bound_endpoint.empty ()) {
        errno = EBUSY;
        return -1;
    }
    _tls_cert = cert_;
    _tls_key = key_;
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
    if (_mesh_client_tls_locked || _registration_tls_locked) {
        errno = EBUSY;
        return -1;
    }
    _tls_ca = ca_cert_ ? ca_cert_ : "";
    _tls_hostname = hostname_ ? hostname_ : "";
    _tls_trust_system = trust_system_;
    return 0;
}

int spot_node_t::set_send_ready_handler (zlink_send_ready_handler_fn handler_,
                                         void *userdata_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    spot_pub_t *pub = ensure_default_pub ();
    if (!pub)
        return -1;

    const int rc = pub->set_send_ready_handler (handler_, this, userdata_);
    if (rc == 0) {
        _send_ready_handler_userdata.store (userdata_,
                                            std::memory_order_release);
        _send_ready_handler.store (handler_, std::memory_order_release);
    }
    return rc;
}

void spot_node_t::queue_service_discovery_refresh_locked (
  const std::string &service_name_)
{
    if (!service_name_.empty ())
        _service_attachment_state.pending_refresh_services.insert (service_name_);
}
}
