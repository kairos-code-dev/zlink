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
static void preserve_first_error_local (int rc_, int *first_error_)
{
    if (rc_ == 0 || !first_error_ || *first_error_ != 0)
        return;
    *first_error_ = errno != 0 ? errno : EIO;
}

static void spot_shutdown_logf_local (bool always_, const char *fmt_, ...)
{
    if (!always_ && !std::getenv ("ZLINK_DEBUG_SPOT_SHUTDOWN"))
        return;

    va_list args;
    va_start (args, fmt_);
    std::fprintf (stderr, "[spot-shutdown] ");
    std::vfprintf (stderr, fmt_, args);
    std::fprintf (stderr, "\n");
    std::fflush (stderr);
    va_end (args);
}

static void copy_service_name_field_local (char *dst_,
                                           size_t dst_size_,
                                           const std::string &src_)
{
    if (!dst_ || dst_size_ == 0)
        return;
    dst_[0] = '\0';
    if (src_.empty ())
        return;
    strncpy (dst_, src_.c_str (), dst_size_ - 1);
    dst_[dst_size_ - 1] = '\0';
}

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

static bool valid_router_attachment_type_local (socket_base_t *socket_)
{
    return socket_ && socket_->check_tag ()
           && (socket_->socket_type () == ZLINK_CORE_SOCKET_ROUTER
               || socket_->socket_type () == ZLINK_CORE_SOCKET_DEALER);
}

static void reset_service_attachment_stats_row_local (
  zlink_spot_service_attachment_stats_t *row_,
  const std::string &service_name_)
{
    if (!row_)
        return;
    memset (row_, 0, sizeof (*row_));
    copy_service_name_field_local (
      row_->service_name, sizeof (row_->service_name), service_name_);
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

int spot_node_t::validate_manual_service_attachment_locked (
  const std::string &service_name_,
  const socket_base_t *primary_socket_,
  const socket_base_t *secondary_socket_) const
{
    (void) service_name_;
    if (_facades.size () > 1) {
        errno = EBUSY;
        return -1;
    }
    if (primary_socket_ && _service_attachment_socket_index.count (primary_socket_) != 0) {
        errno = EBUSY;
        return -1;
    }
    if (secondary_socket_
        && _service_attachment_socket_index.count (secondary_socket_) != 0) {
        errno = EBUSY;
        return -1;
    }
    return 0;
}

void spot_node_t::register_service_monitor_locked (
  socket_base_t *owner_socket_,
  void *monitor_handle_,
  const std::string &service_name_,
  zlink_spot_service_attachment_role_t role_)
{
    service_monitor_handle_t monitor_entry;
    monitor_entry.handle = monitor_handle_;
    monitor_entry.owner_socket = owner_socket_;
    monitor_entry.service_name = service_name_;
    monitor_entry.role = role_;
    _service_monitors.push_back (monitor_entry);
}

void spot_node_t::ensure_service_stats_row_locked (
  const std::string &service_name_)
{
    if (_service_attachment_state.stats_cache.count (service_name_) != 0)
        return;
    zlink_spot_service_attachment_stats_t row;
    reset_service_attachment_stats_row_local (&row, service_name_);
    _service_attachment_state.stats_cache[service_name_] = row;
}

void spot_node_t::erase_service_stats_row_if_unused_locked (
  const std::string &service_name_)
{
    if (_service_attachments.count (service_name_) != 0
        || _service_discoveries.count (service_name_) != 0)
        return;
    _service_attachment_state.stats_cache.erase (service_name_);
}

void spot_node_t::update_service_stats_locked (
  const std::string &service_name_, const service_attachment_t &attachment_)
{
    ensure_service_stats_row_locked (service_name_);
    zlink_spot_service_attachment_stats_t &row =
      _service_attachment_state.stats_cache[service_name_];
    reset_service_attachment_stats_row_local (&row, service_name_);
    row.router_count =
      static_cast<uint32_t> (attachment_.manual.routers.size ());
    row.pub_count = attachment_.manual.pub ? 1u : 0u;
    row.sub_count = attachment_.manual.sub ? 1u : 0u;
    row.auto_router_count = attachment_.auto_router_count ();
    row.auto_pub_count = attachment_.auto_pub_count ();
    row.auto_sub_count = attachment_.auto_sub_count ();
}

void spot_node_t::register_manual_router_locked (const std::string &service_name_,
                                                 socket_base_t *router_,
                                                 void *monitor_handle_)
{
    service_attachment_t &attachment = _service_attachments[service_name_];
    attachment.manual.routers.push_back (router_);
    _service_attachment_socket_index[router_] = service_name_;
    register_service_monitor_locked (router_, monitor_handle_, service_name_,
                                     ZLINK_SPOT_SERVICE_ATTACHMENT_ROUTER);
    update_service_stats_locked (service_name_, attachment);
}

void spot_node_t::register_manual_pubsub_locked (const std::string &service_name_,
                                                 socket_base_t *pub_,
                                                 socket_base_t *sub_,
                                                 void *pub_monitor_handle_,
                                                 void *sub_monitor_handle_)
{
    service_attachment_t &attachment = _service_attachments[service_name_];
    attachment.manual.pub = pub_;
    attachment.manual.sub = sub_;
    _service_attachment_socket_index[pub_] = service_name_;
    _service_attachment_socket_index[sub_] = service_name_;
    register_service_monitor_locked (
      pub_, pub_monitor_handle_, service_name_, ZLINK_SPOT_SERVICE_ATTACHMENT_PUB);
    register_service_monitor_locked (
      sub_, sub_monitor_handle_, service_name_, ZLINK_SPOT_SERVICE_ATTACHMENT_SUB);
    update_service_stats_locked (service_name_, attachment);
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

    if (discovery_->service_type () == discovery_protocol::service_type_socket) {
        const std::string service_name = discovery_->service_name ();
        std::vector<provider_info_t> providers;
        discovery_->snapshot_providers (service_name, &providers);
        bool has_router = false;
        bool has_pub = false;
        bool has_sub = false;
        for (size_t i = 0; i < providers.size (); ++i) {
            if (providers[i].service_role == discovery_protocol::service_role_dealer) {
                errno = ENOTSUP;
                return -1;
            }
            if (providers[i].service_role == discovery_protocol::service_role_router)
                has_router = true;
            else if (providers[i].service_role == discovery_protocol::service_role_pub)
                has_pub = true;
            else if (providers[i].service_role == discovery_protocol::service_role_sub)
                has_sub = true;
            else {
                errno = ENOTSUP;
                return -1;
            }
        }
        if (has_pub != has_sub) {
            errno = EINVAL;
            return -1;
        }

        {
            scoped_lock_t lock (_sync);
            if (validate_socket_service_discovery_attach_locked (service_name,
                                                                 discovery_)
                != 0) {
                return -1;
            }
            _service_discoveries[service_name] = discovery_;
            ensure_service_stats_row_locked (service_name);
            queue_service_discovery_refresh_locked (service_name);
            _summary_last_changed_ms = zlink::clock_t ().now_ms ();
        }
        if (discovery_->add_observer (this) != 0) {
            scoped_lock_t lock (_sync);
            _service_discoveries.erase (service_name);
            _service_attachment_state.pending_refresh_services.erase (service_name);
            erase_service_stats_row_if_unused_locked (service_name);
            return -1;
        }
        wake_control_task ();
        return 0;
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

int spot_node_t::attach_router (const char *service_name_, socket_base_t *router_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!valid_service_name_local (service_name_)
        || !valid_router_attachment_type_local (router_)) {
        errno = EINVAL;
        return -1;
    }
    if (ensure_healthy () != 0)
        return -1;

    zlink_socket_monitor_open_options_t monitor_options;
    memset (&monitor_options, 0, sizeof (monitor_options));
    monitor_options.events = ZLINK_EVENT_ALL;
    void *monitor = zlink_socket_monitor_open (router_, &monitor_options);
    if (!monitor)
        return -1;

    scoped_lock_t lock (_sync);
    if (validate_manual_service_attachment_locked (service_name_, router_) != 0) {
        zlink_monitor_close (&monitor);
        return -1;
    }

    register_manual_router_locked (service_name_, router_, monitor);
    rebuild_service_attachment_caches_locked ();
    return 0;
}

int spot_node_t::attach_pubsub (const char *service_name_,
                                socket_base_t *pub_,
                                socket_base_t *sub_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!valid_service_name_local (service_name_)
        || !valid_attached_socket_type_local (pub_, ZLINK_CORE_SOCKET_PUB)
        || !valid_attached_socket_type_local (sub_, ZLINK_CORE_SOCKET_SUB)) {
        errno = EINVAL;
        return -1;
    }
    if (ensure_healthy () != 0)
        return -1;

    zlink_socket_monitor_open_options_t monitor_options;
    memset (&monitor_options, 0, sizeof (monitor_options));
    monitor_options.events = ZLINK_EVENT_ALL;
    void *pub_monitor = zlink_socket_monitor_open (pub_, &monitor_options);
    if (!pub_monitor)
        return -1;
    void *sub_monitor = zlink_socket_monitor_open (sub_, &monitor_options);
    if (!sub_monitor) {
        zlink_monitor_close (&pub_monitor);
        return -1;
    }

    {
        scoped_lock_t lock (_sync);
        if (validate_manual_service_attachment_locked (service_name_, pub_, sub_)
            != 0) {
            zlink_monitor_close (&pub_monitor);
            zlink_monitor_close (&sub_monitor);
            return -1;
        }

        service_attachment_t &attachment = _service_attachments[service_name_];
        if (attachment.manual.pub || attachment.manual.sub) {
            zlink_monitor_close (&pub_monitor);
            zlink_monitor_close (&sub_monitor);
            errno = EBUSY;
            return -1;
        }
        register_manual_pubsub_locked (service_name_, pub_, sub_, pub_monitor,
                                       sub_monitor);
        rebuild_service_attachment_caches_locked ();
    }

    return apply_service_subscription_filters ();
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

int spot_node_t::validate_destroyable_handles_locked () const
{
    for (std::set<spot_pub_t *>::const_iterator it = _pubs.begin ();
         it != _pubs.end (); ++it) {
        if (*it && !(*it)->is_node_owned_default ()) {
            errno = EBUSY;
            return -1;
        }
    }
    for (std::set<spot_sub_t *>::const_iterator it = _subs.begin ();
         it != _subs.end (); ++it) {
        spot_internal_receiver_t *receiver = _handle_defaults.internal_receiver ();
        if (receiver && *it == receiver->impl ())
            continue;
        if (*it && !(*it)->is_node_owned_default ()) {
            errno = EBUSY;
            return -1;
        }
    }
    return 0;
}

void spot_node_t::begin_destroy_detach_phase (
  discovery_t **discovery_out_,
  std::map<std::string, discovery_t *> *service_discoveries_out_,
  std::vector<std::string> *active_peer_endpoints_out_,
  std::string *bound_endpoint_out_)
{
    if (discovery_out_)
        *discovery_out_ = NULL;
    if (active_peer_endpoints_out_)
        active_peer_endpoints_out_->clear ();
    if (bound_endpoint_out_)
        bound_endpoint_out_->clear ();
    if (service_discoveries_out_)
        service_discoveries_out_->clear ();

    scoped_lock_t lock (_sync);
    if (active_peer_endpoints_out_) {
        active_peer_endpoints_out_->assign (_peer_state.active_endpoints.begin (),
                                            _peer_state.active_endpoints.end ());
    }
    if (bound_endpoint_out_)
        *bound_endpoint_out_ = _bound_endpoint;
    if (discovery_out_)
        *discovery_out_ = _discovery;

    reset_spot_discovery_state_locked ();
    _peer_state.manual_endpoints.clear ();
    _peer_state.active_endpoints.clear ();
    _active_peer_count.store (0, std::memory_order_release);
    if (service_discoveries_out_)
        service_discoveries_out_->swap (_service_discoveries);
}

void spot_node_t::clear_service_attachment_runtime_locked (
  std::deque<service_monitor_handle_t> *monitors_out_)
{
    if (!monitors_out_)
        return;
    monitors_out_->clear ();
    scoped_lock_t lock (_sync);
    monitors_out_->swap (_service_monitors);
    _service_attachments.clear ();
    _service_attachment_socket_index.clear ();
    _service_attachment_state.pending_refresh_services.clear ();
    _service_attachment_state.stats_cache.clear ();
    rebuild_service_attachment_caches_locked ();
}

void spot_node_t::close_service_monitors (
  std::deque<service_monitor_handle_t> *monitors_)
{
    if (!monitors_)
        return;
    for (std::deque<service_monitor_handle_t>::iterator it = monitors_->begin ();
         it != monitors_->end (); ++it) {
        if (it->handle)
            (void) zlink_monitor_close (&it->handle);
    }
}

int spot_node_t::destroy ()
{
    {
        scoped_lock_t lock (_sync);
        if (validate_destroyable_handles_locked () != 0)
            return -1;
    }
    _lifecycle.transition_to (service_state_stopping);
    discovery_t *discovery = NULL;
    std::map<std::string, discovery_t *> service_discoveries;
    std::vector<std::string> active_peer_endpoints;
    std::string bound_endpoint;
    int first_error = 0;
    int graceful_error = 0;
    int final_error = 0;
    bool used_abortive = false;

    spot_shutdown_logf_local (false,
                              "step=begin node=%p service=%s state=%d tracked=%zu",
                              static_cast<void *> (this),
                              _discovery_service.c_str (),
                              static_cast<int> (_lifecycle.state ()),
                              _lifecycle.owned_socket_count ());
    if (_discovery && _registered)
        (void) unregister_registered ();
    begin_destroy_detach_phase (&discovery, &service_discoveries,
                                &active_peer_endpoints, &bound_endpoint);
    for (size_t i = 0; i < active_peer_endpoints.size (); ++i)
        (void) send_data_plane_command ("disconnect_peer_pub",
                                        active_peer_endpoints[i].c_str ());
    if (!bound_endpoint.empty ())
        (void) send_data_plane_command ("unbind_pub", bound_endpoint.c_str ());
    spot_shutdown_logf_local (false, "step=peer_disconnect node=%p",
                              static_cast<void *> (this));
    if (_runtime)
        _runtime->stop.set (1);
    submit_stopped_summaries ();
    spot_shutdown_logf_local (false, "step=summaries_stopped node=%p",
                              static_cast<void *> (this));

    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    const uint64_t control_task_id =
      _runtime ? _runtime->clear_control_task_id () : 0;
    if (runtime && control_task_id != 0)
        runtime->remove_task (control_task_id);
    spot_shutdown_logf_local (false, "step=task_removed node=%p",
                              static_cast<void *> (this));

    std::deque<service_monitor_handle_t> monitors;
    clear_service_attachment_runtime_locked (&monitors);
    close_service_monitors (&monitors);

    if (discovery)
        preserve_first_error_local (discovery->remove_observer (this),
                                    &first_error);
    for (std::map<std::string, discovery_t *>::iterator it =
           service_discoveries.begin ();
         it != service_discoveries.end (); ++it) {
        if (it->second)
            preserve_first_error_local (it->second->remove_observer (this),
                                        &first_error);
    }
    spot_shutdown_logf_local (false, "step=observer_removed node=%p",
                              static_cast<void *> (this));

    if (_runtime)
        preserve_first_error_local (_runtime->stop_and_join (), &first_error);
    spot_shutdown_logf_local (false,
                              "step=data_plane_stopped node=%p error=%d",
                              static_cast<void *> (this), first_error);
    preserve_first_error_local (destroy_handles (), &first_error);
    preserve_first_error_local (destroy_internal_receiver (), &first_error);
    if (_runtime)
        preserve_first_error_local (_runtime->close_control_sockets (),
                                    &first_error);
    spot_shutdown_logf_local (false,
                              "step=handles_destroyed node=%p error=%d tracked=%zu",
                              static_cast<void *> (this), first_error,
                              _lifecycle.owned_socket_count ());
    if (first_error == 0 && _runtime && _runtime->attachment_count () == 0
        && _runtime->live_socket_slot_count () == 0
        && _lifecycle.owned_socket_count () != 0) {
        spot_shutdown_logf_local (
          false, "step=clear_tracked_sockets node=%p tracked=%zu",
          static_cast<void *> (this), _lifecycle.owned_socket_count ());
        _lifecycle.clear_tracked_sockets ();
    }
    preserve_first_error_local (wait_owned_socket_removals (10000),
                                &first_error);
    graceful_error = first_error;
    final_error = graceful_error;

    if (_runtime
        && (first_error != 0 || _runtime->live_socket_slot_count () != 0
            || _runtime->attachment_count () != 0)) {
        const int abort_reason = first_error != 0 ? first_error : ETIMEDOUT;
        const size_t live_slots = _runtime->live_socket_slot_count ();
        const size_t live_attachments = _runtime->attachment_count ();
        const size_t tracked_sockets = _lifecycle.owned_socket_count ();
        size_t ctx_socket_baseline = 0;
        if (_ctx) {
            const size_t ctx_socket_count = _ctx->socket_count ();
            ctx_socket_baseline =
              ctx_socket_count > tracked_sockets ? ctx_socket_count - tracked_sockets
                                                 : 0;
        }
        used_abortive = true;
        spot_shutdown_logf_local (
          true,
          "service=spot node=%p shutdown=abortive reason=%d live_slots=%zu attachments=%zu tracked=%zu",
          static_cast<void *> (this), abort_reason, live_slots,
          live_attachments, tracked_sockets);
        _runtime->abortive_stop ();
        preserve_first_error_local (_lifecycle.force_wait_remaining (5000),
                                    &final_error);
        preserve_first_error_local (wait_owned_socket_removals (5000),
                                    &final_error);
        if (_runtime->live_socket_slot_count () == 0
            && _runtime->attachment_count () == 0) {
            if (_lifecycle.owned_socket_count () != 0)
                _lifecycle.clear_tracked_sockets ();

            if (_lifecycle.owned_socket_count () == 0) {
                final_error = 0;
            } else if (_ctx
                       && _ctx->wait_for_socket_count_at_most (
                            ctx_socket_baseline, 5000)
                            == 0) {
                _lifecycle.clear_tracked_sockets ();
                final_error = 0;
            } else if (_ctx && _ctx->socket_count () == 0) {
                _lifecycle.clear_tracked_sockets ();
                final_error = 0;
            }
        }
    }

    if (!used_abortive)
        spot_shutdown_logf_local (false,
                                  "service=spot node=%p shutdown=graceful",
                                  static_cast<void *> (this));
    _lifecycle.transition_to (service_state_stopped);
    spot_shutdown_logf_local (false,
                              "step=complete node=%p state=%d error=%d tracked=%zu",
                              static_cast<void *> (this),
                              static_cast<int> (_lifecycle.state ()),
                              final_error, _lifecycle.owned_socket_count ());
    if (final_error != 0) {
        errno = final_error;
        return -1;
    }
    return 0;
}

void spot_node_t::queue_service_discovery_refresh_locked (
  const std::string &service_name_)
{
    if (!service_name_.empty ())
        _service_attachment_state.pending_refresh_services.insert (service_name_);
}
}
