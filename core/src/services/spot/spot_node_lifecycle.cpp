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

template <typename T, typename SocketSelector>
static bool wait_for_service_socket_event_local (const T &items_,
                                                 short events_,
                                                 zlink_recv_flags_t flags_,
                                                 size_t *ready_index_out_,
                                                 SocketSelector socket_selector_)
{
    if (ready_index_out_)
        *ready_index_out_ = 0;
    if (items_.empty ()) {
        errno = (flags_ & ZLINK_DONTWAIT) != 0 ? EAGAIN : ENOTCONN;
        return false;
    }

    const bool dontwait = (flags_ & ZLINK_DONTWAIT) != 0;
    for (size_t attempt = 0; attempt < items_.size (); ++attempt) {
        const int timeout_ms =
          dontwait || attempt + 1 < items_.size () ? 0 : 25;
        if (zlink::wait_socket_events_internal (
              socket_selector_ (items_[attempt]), events_, timeout_ms)
            > 0) {
            if (ready_index_out_)
                *ready_index_out_ = attempt;
            return true;
        }
    }
    errno = EAGAIN;
    return false;
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
            if (providers[i].service_role == discovery_protocol::service_role_router
                || providers[i].service_role == discovery_protocol::service_role_dealer)
                has_router = true;
            else if (providers[i].service_role == discovery_protocol::service_role_pub)
                has_pub = true;
            else if (providers[i].service_role == discovery_protocol::service_role_sub)
                has_sub = true;
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

void spot_node_t::rebuild_service_attachment_caches_locked ()
{
    std::shared_ptr<service_attachment_state_t::service_sub_cache_t> sub_cache (
      new service_attachment_state_t::service_sub_cache_t ());
    std::shared_ptr<service_attachment_state_t::readable_sub_cache_t>
      readable_sub_cache (new service_attachment_state_t::readable_sub_cache_t ());
    std::shared_ptr<socket_poller_t> readable_sub_poller (
      new socket_poller_t ());
    std::shared_ptr<service_attachment_state_t::service_monitor_cache_t>
      monitor_cache (new service_attachment_state_t::service_monitor_cache_t ());

    sub_cache->reserve (_service_attachments.size () * 2);
    readable_sub_cache->reserve (_service_attachments.size () * 2);
    monitor_cache->reserve (_service_monitors.size ());
    for (std::map<std::string, service_attachment_t>::const_iterator it =
           _service_attachments.begin ();
         it != _service_attachments.end (); ++it) {
        update_service_stats_locked (it->first, it->second);
        if (it->second.has_manual_pubsub ()) {
            service_attachment_state_t::service_sub_cache_entry_t entry;
            entry.service_name = it->first;
            entry.socket = it->second.manual.sub;
            sub_cache->push_back (entry);
            readable_sub_cache->push_back (it->second.manual.sub);
            (void) readable_sub_poller->add (it->second.manual.sub, NULL,
                                             ZLINK_POLLIN);
        }
        if (it->second.has_auto_pubsub ()) {
            service_attachment_state_t::service_sub_cache_entry_t entry;
            entry.service_name = it->first;
            entry.socket = it->second.discovered.sub;
            sub_cache->push_back (entry);
            readable_sub_cache->push_back (it->second.discovered.sub);
            (void) readable_sub_poller->add (it->second.discovered.sub, NULL,
                                             ZLINK_POLLIN);
        }
    }
    for (std::deque<service_monitor_handle_t>::const_iterator it =
           _service_monitors.begin ();
         it != _service_monitors.end (); ++it)
        monitor_cache->push_back (*it);

    for (service_attachment_state_t::service_stats_cache_t::iterator it =
           _service_attachment_state.stats_cache.begin ();
         it != _service_attachment_state.stats_cache.end ();) {
        if (_service_attachments.count (it->first) == 0
            && _service_discoveries.count (it->first) == 0)
            it = _service_attachment_state.stats_cache.erase (it);
        else
            ++it;
    }

    _service_attachment_state.sub_cache = sub_cache;
    _service_attachment_state.readable_sub_cache = readable_sub_cache;
    _service_attachment_state.readable_sub_poller = readable_sub_poller;
    _service_attachment_state.monitor_cache = monitor_cache;
}

void spot_node_t::queue_service_discovery_refresh_locked (
  const std::string &service_name_)
{
    if (!service_name_.empty ())
        _service_attachment_state.pending_refresh_services.insert (service_name_);
}

void spot_node_t::remove_service_monitors_by_owner_locked (
  const std::vector<socket_base_t *> &sockets_)
{
    if (sockets_.empty ())
        return;
    for (std::deque<service_monitor_handle_t>::iterator mit =
           _service_monitors.begin ();
         mit != _service_monitors.end ();) {
        bool matched = false;
        for (size_t i = 0; i < sockets_.size (); ++i) {
            if (mit->owner_socket == sockets_[i]) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            ++mit;
            continue;
        }
        if (mit->handle)
            (void) zlink_monitor_close (&mit->handle);
        mit = _service_monitors.erase (mit);
    }
}

bool spot_node_t::detach_discovered_service_locked (
  discovery_t *discovery_, std::vector<socket_base_t *> *sockets_to_close_out_)
{
    if (!sockets_to_close_out_)
        return false;
    sockets_to_close_out_->clear ();

    for (std::map<std::string, discovery_t *>::iterator it =
           _service_discoveries.begin ();
         it != _service_discoveries.end (); ++it) {
        if (it->second != discovery_)
            continue;
        const std::string service_name = it->first;

        std::map<std::string, service_attachment_t>::iterator attach_it =
          _service_attachments.find (service_name);
        if (attach_it != _service_attachments.end ()) {
            for (std::map<std::string, socket_base_t *>::iterator rit =
                   attach_it->second.discovered.routers.begin ();
                 rit != attach_it->second.discovered.routers.end (); ++rit) {
                if (rit->second) {
                    sockets_to_close_out_->push_back (rit->second);
                    _service_attachment_socket_index.erase (rit->second);
                }
            }
            if (attach_it->second.discovered.pub) {
                sockets_to_close_out_->push_back (attach_it->second.discovered.pub);
                _service_attachment_socket_index.erase (
                  attach_it->second.discovered.pub);
            }
            if (attach_it->second.discovered.sub) {
                sockets_to_close_out_->push_back (attach_it->second.discovered.sub);
                _service_attachment_socket_index.erase (
                  attach_it->second.discovered.sub);
            }

            attach_it->second.discovered.routers.clear ();
            attach_it->second.discovered.pub = NULL;
            attach_it->second.discovered.sub = NULL;
            attach_it->second.discovered.router_endpoints.clear ();
            attach_it->second.discovered.pub_endpoints.clear ();
            attach_it->second.discovered.sub_endpoints.clear ();
            attach_it->second.clear_auto_sub_replay ();
            update_service_stats_locked (service_name, attach_it->second);
            if (attach_it->second.manual.routers.empty ()
                && !attach_it->second.has_manual_pubsub ()) {
                _service_attachments.erase (attach_it);
                erase_service_stats_row_if_unused_locked (service_name);
            }
        }

        remove_service_monitors_by_owner_locked (*sockets_to_close_out_);
        _service_discoveries.erase (it);
        _service_attachment_state.pending_refresh_services.erase (service_name);
        erase_service_stats_row_if_unused_locked (service_name);
        rebuild_service_attachment_caches_locked ();
        spot_shutdown_logf_local (
          false, "step=detach_discovered_service node=%p sockets=%zu",
          static_cast<void *> (this), sockets_to_close_out_->size ());
        return true;
    }
    return false;
}

socket_base_t *spot_node_t::select_service_router (
  const std::string &service_name_)
{
    scoped_lock_t lock (_sync);
    std::map<std::string, service_attachment_t>::iterator it =
      _service_attachments.find (service_name_);
    if (it == _service_attachments.end ()) {
        errno = _service_discoveries.count (service_name_) != 0 ? ENOTCONN : ENOENT;
        return NULL;
    }
    service_attachment_t &attachment = it->second;
    std::vector<socket_base_t *> candidates;
    candidates.reserve (attachment.manual.routers.size ()
                        + attachment.discovered.routers.size ());
    candidates.insert (candidates.end (), attachment.manual.routers.begin (),
                       attachment.manual.routers.end ());
    for (std::map<std::string, socket_base_t *>::const_iterator it =
           attachment.discovered.routers.begin ();
         it != attachment.discovered.routers.end (); ++it) {
        candidates.push_back (it->second);
    }
    const size_t candidate_count = candidates.size ();
    if (candidate_count == 0) {
        errno = ENOTCONN;
        return NULL;
    }
    for (size_t attempt = 0; attempt < candidate_count; ++attempt) {
        if (attachment.next_router_index >= candidate_count)
            attachment.next_router_index = 0;
        socket_base_t *router = candidates[attachment.next_router_index];
        attachment.next_router_index =
          (attachment.next_router_index + 1) % candidate_count;
        if (router
            && zlink::wait_socket_events_internal (router, ZLINK_POLLOUT, 0) > 0)
            return router;
    }
    errno = ENOTCONN;
    return NULL;
}

socket_base_t *spot_node_t::service_pub_socket (
  const std::string &service_name_) const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    std::map<std::string, service_attachment_t>::const_iterator it =
      _service_attachments.find (service_name_);
    if (it == _service_attachments.end ()) {
        errno = _service_discoveries.count (service_name_) != 0 ? ENOTCONN : ENOENT;
        return NULL;
    }
    if (it->second.has_manual_pubsub ())
        return it->second.manual.pub;
    if (!it->second.has_auto_pubsub ()) {
        errno = ENOTCONN;
        return NULL;
    }
    return it->second.discovered.pub;
}

int spot_node_t::apply_service_subscription_filters ()
{
    std::set<std::string> filters;
    snapshot_raw_subscription_filters (&filters);

    std::vector<std::pair<socket_base_t *, std::set<std::string> > > work;
    const auto collect_filter_work =
      [&] (std::vector<std::pair<socket_base_t *, std::set<std::string> > > *out_) {
        if (!out_)
            return;
        scoped_lock_t lock (_sync);
        for (std::map<std::string, service_attachment_t>::iterator it =
               _service_attachments.begin ();
             it != _service_attachments.end (); ++it) {
            if (it->second.applied_filters == filters)
                continue;
            if (it->second.manual.sub)
                out_->push_back (
                  std::make_pair (it->second.manual.sub, it->second.applied_filters));
            if (it->second.has_auto_pubsub ())
                out_->push_back (std::make_pair (it->second.discovered.sub,
                                                 it->second.applied_filters));
        }
    };
    const auto commit_applied_filters = [&] () {
        scoped_lock_t lock (_sync);
        for (std::map<std::string, service_attachment_t>::iterator it =
               _service_attachments.begin ();
             it != _service_attachments.end (); ++it) {
            if (it->second.manual.sub || it->second.discovered.sub)
                it->second.applied_filters = filters;
        }
    };

    collect_filter_work (&work);

    for (size_t i = 0; i < work.size (); ++i) {
        socket_base_t *sub = work[i].first;
        const std::set<std::string> &applied = work[i].second;
        for (std::set<std::string>::const_iterator it = applied.begin ();
             it != applied.end (); ++it) {
            if (filters.count (*it) == 0 && zlink_unset_subscription (sub, it->c_str ()) != 0)
                return -1;
        }
        for (std::set<std::string>::const_iterator it = filters.begin ();
             it != filters.end (); ++it) {
            if (applied.count (*it) == 0 && zlink_set_subscription (sub, it->c_str ()) != 0)
                return -1;
        }
    }

    commit_applied_filters ();
    return 0;
}

void spot_node_t::collect_pending_service_discoveries_locked (
  std::vector<std::pair<std::string, discovery_t *> > *out_)
{
    if (!out_)
        return;
    out_->clear ();
    for (std::set<std::string>::const_iterator it =
           _service_attachment_state.pending_refresh_services.begin ();
         it != _service_attachment_state.pending_refresh_services.end (); ++it) {
        std::map<std::string, discovery_t *>::const_iterator dit =
          _service_discoveries.find (*it);
        if (dit != _service_discoveries.end ())
            out_->push_back (*dit);
    }
    _service_attachment_state.pending_refresh_services.clear ();
}

void spot_node_t::snapshot_service_discovery_topology (
  discovery_t *discovery_,
  const std::string &service_name_,
  std::vector<provider_info_t> *provider_scratch_,
  service_discovery_topology_t *out_) const
{
    if (!discovery_ || !provider_scratch_ || !out_)
        return;
    out_->clear ();
    provider_scratch_->clear ();
    discovery_->snapshot_providers (service_name_, provider_scratch_);
    for (size_t i = 0; i < provider_scratch_->size (); ++i) {
        const provider_info_t &provider = (*provider_scratch_)[i];
        if (provider.endpoint.empty ())
            continue;
        if (provider.service_role == discovery_protocol::service_role_router
            || provider.service_role == discovery_protocol::service_role_dealer) {
            out_->router_endpoints.insert (provider.endpoint);
        } else if (provider.service_role
                   == discovery_protocol::service_role_pub) {
            out_->pub_endpoints.insert (provider.endpoint);
        } else if (provider.service_role
                   == discovery_protocol::service_role_sub) {
            out_->sub_endpoints.insert (provider.endpoint);
        }
    }
}

spot_node_t::service_discovery_socket_plan_t
spot_node_t::plan_service_discovery_sockets_locked (
  const std::string &service_name_, const service_discovery_topology_t &topology_)
{
    service_discovery_socket_plan_t plan;
    service_attachment_t &attachment = _service_attachments[service_name_];
    const bool pub_endpoints_changed =
      attachment.discovered.pub_endpoints != topology_.pub_endpoints;
    for (std::set<std::string>::const_iterator it =
           topology_.router_endpoints.begin ();
         it != topology_.router_endpoints.end (); ++it) {
        if (attachment.discovered.routers.count (*it) == 0) {
            socket_base_t *router_socket =
              _ctx->create_socket (ZLINK_CORE_SOCKET_DEALER);
            if (router_socket)
                plan.new_router_sockets.push_back (std::make_pair (*it, router_socket));
        }
    }
    if (!attachment.discovered.pub && topology_.pubsub_active ())
        plan.pub_socket = _ctx->create_socket (ZLINK_CORE_SOCKET_PUB);
    if (!attachment.discovered.sub && topology_.pubsub_active ())
        plan.sub_socket = _ctx->create_socket (ZLINK_CORE_SOCKET_SUB);
    if (topology_.pubsub_active () && pub_endpoints_changed) {
        attachment.mark_auto_sub_replay_pending (
          service_attachment_t::discovered_state_t::auto_sub_replay_reconnect);
    } else if (!topology_.pubsub_active ()) {
        attachment.clear_auto_sub_replay ();
    }
    return plan;
}

void spot_node_t::install_service_discovery_sockets (
  const std::string &service_name_,
  const service_discovery_socket_plan_t &plan_,
  const std::set<std::string> &current_filters_)
{
    bool mutated = false;
    for (size_t i = 0; i < plan_.new_router_sockets.size (); ++i) {
        socket_base_t *router_socket = plan_.new_router_sockets[i].second;
        track_owned_socket (router_socket);
        zlink_socket_monitor_open_options_t options;
        memset (&options, 0, sizeof (options));
        options.events = ZLINK_EVENT_ALL;
        void *monitor = zlink_socket_monitor_open (router_socket, &options);
        scoped_lock_t lock (_sync);
        service_attachment_t &attachment = _service_attachments[service_name_];
        if (attachment.discovered.routers.count (plan_.new_router_sockets[i].first)
            == 0) {
            attachment.discovered.routers[plan_.new_router_sockets[i].first] =
              router_socket;
            _service_attachment_socket_index[router_socket] = service_name_;
            register_service_monitor_locked (
              router_socket, monitor, service_name_,
              ZLINK_SPOT_SERVICE_ATTACHMENT_ROUTER);
            update_service_stats_locked (service_name_, attachment);
            mutated = true;
        } else {
            if (monitor)
                (void) zlink_monitor_close (&monitor);
            _ctx->close_socket_and_wait (router_socket, 1000);
            untrack_owned_socket (router_socket);
        }
    }
    if (plan_.pub_socket) {
        track_owned_socket (plan_.pub_socket);
        zlink_socket_monitor_open_options_t options;
        memset (&options, 0, sizeof (options));
        options.events = ZLINK_EVENT_ALL;
        void *monitor = zlink_socket_monitor_open (plan_.pub_socket, &options);
        scoped_lock_t lock (_sync);
        service_attachment_t &attachment = _service_attachments[service_name_];
        if (!attachment.discovered.pub) {
            attachment.discovered.pub = plan_.pub_socket;
            register_service_monitor_locked (
              plan_.pub_socket, monitor, service_name_,
              ZLINK_SPOT_SERVICE_ATTACHMENT_PUB);
            update_service_stats_locked (service_name_, attachment);
            mutated = true;
        } else {
            if (monitor)
                (void) zlink_monitor_close (&monitor);
            socket_base_t *pub_socket = plan_.pub_socket;
            _ctx->close_socket_and_wait (pub_socket, 1000);
            untrack_owned_socket (plan_.pub_socket);
        }
    }
    if (plan_.sub_socket) {
        track_owned_socket (plan_.sub_socket);
        for (std::set<std::string>::const_iterator it = current_filters_.begin ();
             it != current_filters_.end (); ++it) {
            (void) zlink_set_subscription (plan_.sub_socket, it->c_str ());
        }
        zlink_socket_monitor_open_options_t options;
        memset (&options, 0, sizeof (options));
        options.events = ZLINK_EVENT_ALL;
        void *monitor = zlink_socket_monitor_open (plan_.sub_socket, &options);
        scoped_lock_t lock (_sync);
        service_attachment_t &attachment = _service_attachments[service_name_];
        if (!attachment.discovered.sub) {
            attachment.discovered.sub = plan_.sub_socket;
            attachment.mark_auto_sub_replay_pending (
              service_attachment_t::discovered_state_t::auto_sub_replay_initial);
            register_service_monitor_locked (
              plan_.sub_socket, monitor, service_name_,
              ZLINK_SPOT_SERVICE_ATTACHMENT_SUB);
            update_service_stats_locked (service_name_, attachment);
            mutated = true;
        } else {
            if (monitor)
                (void) zlink_monitor_close (&monitor);
            socket_base_t *sub_socket = plan_.sub_socket;
            _ctx->close_socket_and_wait (sub_socket, 1000);
            untrack_owned_socket (plan_.sub_socket);
        }
    }
    if (mutated) {
        scoped_lock_t lock (_sync);
        rebuild_service_attachment_caches_locked ();
    }
}

void spot_node_t::sync_service_discovery_topology (
  const std::string &service_name_, const service_discovery_topology_t &topology_)
{
    service_attachment_t::discovered_state_t discovered_snapshot;
    {
        scoped_lock_t lock (_sync);
        discovered_snapshot = _service_attachments[service_name_].discovered;
    }

    std::vector<socket_base_t *> removed_router_sockets;
    for (std::map<std::string, socket_base_t *>::iterator it =
           discovered_snapshot.routers.begin ();
         it != discovered_snapshot.routers.end (); ++it) {
        if (topology_.router_endpoints.count (it->first) == 0)
            removed_router_sockets.push_back (it->second);
    }

    for (std::map<std::string, socket_base_t *>::iterator it =
           discovered_snapshot.routers.begin ();
         it != discovered_snapshot.routers.end (); ++it) {
        if (topology_.router_endpoints.count (it->first) != 0) {
            if (discovered_snapshot.router_endpoints.count (it->first) == 0)
                (void) it->second->connect (it->first.c_str ());
        } else {
            (void) it->second->term_endpoint (it->first.c_str ());
        }
    }

    if (discovered_snapshot.pub) {
        for (std::set<std::string>::const_iterator it =
               discovered_snapshot.sub_endpoints.begin ();
             it != discovered_snapshot.sub_endpoints.end (); ++it) {
            if (topology_.sub_endpoints.count (*it) == 0)
                (void) discovered_snapshot.pub->term_endpoint (it->c_str ());
        }
        if (topology_.pubsub_active ()) {
            for (std::set<std::string>::const_iterator it =
                   topology_.sub_endpoints.begin ();
                 it != topology_.sub_endpoints.end (); ++it) {
                if (discovered_snapshot.sub_endpoints.count (*it) == 0)
                    (void) discovered_snapshot.pub->connect (it->c_str ());
            }
        }
    }

    if (discovered_snapshot.sub) {
        for (std::set<std::string>::const_iterator it =
               discovered_snapshot.pub_endpoints.begin ();
             it != discovered_snapshot.pub_endpoints.end (); ++it) {
            if (topology_.pub_endpoints.count (*it) == 0)
                (void) discovered_snapshot.sub->term_endpoint (it->c_str ());
        }
        if (topology_.pubsub_active ()) {
            for (std::set<std::string>::const_iterator it =
                   topology_.pub_endpoints.begin ();
                 it != topology_.pub_endpoints.end (); ++it) {
                if (discovered_snapshot.pub_endpoints.count (*it) == 0)
                    (void) discovered_snapshot.sub->connect (it->c_str ());
            }
        }
    }

    {
        scoped_lock_t lock (_sync);
        service_attachment_t &attachment = _service_attachments[service_name_];
        std::vector<socket_base_t *> stale_router_sockets;
        for (std::map<std::string, socket_base_t *>::iterator it =
               attachment.discovered.routers.begin ();
             it != attachment.discovered.routers.end ();) {
            if (topology_.router_endpoints.count (it->first) == 0) {
                stale_router_sockets.push_back (it->second);
                _service_attachment_socket_index.erase (it->second);
                it = attachment.discovered.routers.erase (it);
            } else {
                ++it;
            }
        }
        remove_service_monitors_by_owner_locked (stale_router_sockets);
        attachment.discovered.router_endpoints = topology_.router_endpoints;
        attachment.discovered.pub_endpoints =
          topology_.pubsub_active () ? topology_.pub_endpoints
                                     : std::set<std::string> ();
        attachment.discovered.sub_endpoints =
          topology_.pubsub_active () ? topology_.sub_endpoints
                                     : std::set<std::string> ();
        update_service_stats_locked (service_name_, attachment);
        rebuild_service_attachment_caches_locked ();
    }

    for (size_t i = 0; i < removed_router_sockets.size (); ++i) {
        _ctx->close_socket_and_wait (removed_router_sockets[i], 1000);
        untrack_owned_socket (removed_router_sockets[i]);
    }
}

void spot_node_t::replay_pending_service_discovery_filters (
  const std::string &service_name_, const std::set<std::string> &current_filters_)
{
    socket_base_t *auto_sub = NULL;
    bool needs_replay = false;
    {
        scoped_lock_t lock (_sync);
        std::map<std::string, service_attachment_t>::const_iterator it =
          _service_attachments.find (service_name_);
        if (it != _service_attachments.end ()) {
            auto_sub = it->second.discovered.sub;
            needs_replay = it->second.needs_auto_sub_replay ();
        }
    }
    if (!auto_sub || !needs_replay)
        return;
    for (std::set<std::string>::const_iterator it = current_filters_.begin ();
         it != current_filters_.end (); ++it) {
        (void) zlink_set_subscription (auto_sub, it->c_str ());
    }
    scoped_lock_t lock (_sync);
    std::map<std::string, service_attachment_t>::iterator it =
      _service_attachments.find (service_name_);
    if (it != _service_attachments.end () && it->second.discovered.sub == auto_sub)
        it->second.clear_auto_sub_replay ();
}

void spot_node_t::refresh_service_discovery_attachments ()
{
    std::vector<std::pair<std::string, discovery_t *> > discoveries;
    std::set<std::string> current_filters;
    {
        scoped_lock_t lock (_sync);
        collect_pending_service_discoveries_locked (&discoveries);
    }
    if (discoveries.empty ())
        return;
    snapshot_raw_subscription_filters (&current_filters);

    std::vector<provider_info_t> provider_scratch;
    service_discovery_topology_t topology_scratch;

    for (size_t i = 0; i < discoveries.size (); ++i) {
        snapshot_service_discovery_topology (
          discoveries[i].second, discoveries[i].first, &provider_scratch,
          &topology_scratch);
        service_discovery_socket_plan_t plan;
        {
            scoped_lock_t lock (_sync);
            plan =
              plan_service_discovery_sockets_locked (discoveries[i].first,
                                                     topology_scratch);
        }
        install_service_discovery_sockets (discoveries[i].first, plan,
                                           current_filters);
        sync_service_discovery_topology (discoveries[i].first, topology_scratch);
        replay_pending_service_discovery_filters (discoveries[i].first,
                                                  current_filters);
    }

    (void) apply_service_subscription_filters ();
}

int spot_node_t::snapshot_service_attachments (
  std::vector<zlink_spot_service_attachment_stats_t> *out_) const
{
    if (!out_) {
        errno = EFAULT;
        return -1;
    }
    out_->clear ();
    {
        scoped_lock_t lock (const_cast<mutex_t &> (_sync));
        out_->reserve (_service_attachment_state.stats_cache.size ());
        for (service_attachment_state_t::service_stats_cache_t::const_iterator it =
               _service_attachment_state.stats_cache.begin ();
             it != _service_attachment_state.stats_cache.end (); ++it) {
            zlink_spot_service_attachment_stats_t row = it->second;
            out_->push_back (row);
        }
    }
    return 0;
}

int spot_node_t::service_subscribe_recv (zlink_routing_id_t *source_rid_out_,
                                         zlink_msg_t **parts_out_,
                                         size_t *part_count_out_,
                                         char *service_name_out_,
                                         size_t *service_name_len_out_,
                                         char *topic_id_out_,
                                         size_t *topic_id_len_out_,
                                         zlink_recv_flags_t flags_)
{
    if (!parts_out_ || !part_count_out_ || !topic_id_len_out_
        || !service_name_len_out_) {
        errno = EFAULT;
        return -1;
    }

    while (true) {
        std::shared_ptr<const service_attachment_state_t::service_sub_cache_t>
          subs;
        {
            scoped_lock_t lock (_sync);
            subs = _service_attachment_state.sub_cache;
        }
        size_t ready_index = 0;
        if (!wait_for_service_socket_event_local (
              *subs, ZLINK_POLLIN, flags_, &ready_index,
              [] (const service_attachment_state_t::service_sub_cache_entry_t
                    &entry_) -> socket_base_t * { return entry_.socket; })) {
            if ((flags_ & ZLINK_DONTWAIT) != 0)
                return -1;
            continue;
        }

        size_t service_len = *service_name_len_out_;
        zlink_recv_result_t rc =
          zlink_subscribe ((*subs)[ready_index].socket, source_rid_out_,
                           parts_out_,
                           part_count_out_, topic_id_out_, topic_id_len_out_,
                           static_cast<zlink_recv_flags_t> (ZLINK_DONTWAIT));
        if (rc == ZLINK_RECV_NO_DATA) {
            if ((flags_ & ZLINK_DONTWAIT) != 0) {
                errno = EAGAIN;
                return -1;
            }
            continue;
        }
        if (rc != ZLINK_RECV_OK)
            return -1;
        if (!service_name_out_) {
            *service_name_len_out_ = (*subs)[ready_index].service_name.size ();
            return 0;
        }
        if (service_len < (*subs)[ready_index].service_name.size ()) {
            *service_name_len_out_ = (*subs)[ready_index].service_name.size ();
            errno = EMSGSIZE;
            return -1;
        }
        if (!(*subs)[ready_index].service_name.empty ())
            memcpy (service_name_out_,
                    (*subs)[ready_index].service_name.data (),
                    (*subs)[ready_index].service_name.size ());
        *service_name_len_out_ = (*subs)[ready_index].service_name.size ();
        return 0;
    }
}

int spot_node_t::service_subscription_event_recv (
  zlink_routing_id_t *source_rid_out_,
  int *subscribed_out_,
  char *service_name_out_,
  size_t *service_name_len_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_)
{
    if (!subscribed_out_ || !topic_id_len_out_ || !service_name_len_out_) {
        errno = EFAULT;
        return -1;
    }

    while (true) {
        std::shared_ptr<const service_attachment_state_t::service_sub_cache_t>
          subs;
        {
            scoped_lock_t lock (_sync);
            subs = _service_attachment_state.sub_cache;
        }
        size_t ready_index = 0;
        if (!wait_for_service_socket_event_local (
              *subs, ZLINK_POLLIN, flags_, &ready_index,
              [] (const service_attachment_state_t::service_sub_cache_entry_t
                    &entry_) -> socket_base_t * { return entry_.socket; })) {
            if ((flags_ & ZLINK_DONTWAIT) != 0)
                return -1;
            continue;
        }

        size_t service_len = *service_name_len_out_;
        zlink_recv_result_t rc =
          zlink_subscription_event ((*subs)[ready_index].socket,
                                    source_rid_out_,
                                    subscribed_out_, topic_id_out_,
                                    topic_id_len_out_,
                                    static_cast<zlink_recv_flags_t> (
                                      ZLINK_DONTWAIT));
        if (rc == ZLINK_RECV_NO_DATA) {
            if ((flags_ & ZLINK_DONTWAIT) != 0) {
                errno = EAGAIN;
                return -1;
            }
            continue;
        }
        if (rc != ZLINK_RECV_OK)
            return -1;
        if (!service_name_out_) {
            *service_name_len_out_ = (*subs)[ready_index].service_name.size ();
            return 0;
        }
        if (service_len < (*subs)[ready_index].service_name.size ()) {
            *service_name_len_out_ = (*subs)[ready_index].service_name.size ();
            errno = EMSGSIZE;
            return -1;
        }
        if (!(*subs)[ready_index].service_name.empty ())
            memcpy (service_name_out_,
                    (*subs)[ready_index].service_name.data (),
                    (*subs)[ready_index].service_name.size ());
        *service_name_len_out_ = (*subs)[ready_index].service_name.size ();
        return 0;
    }
}

int spot_node_t::service_monitor_recv (zlink_spot_service_monitor_event_t *out_,
                                       zlink_recv_flags_t flags_)
{
    if (!out_) {
        errno = EFAULT;
        return -1;
    }

    while (true) {
        std::shared_ptr<const service_attachment_state_t::service_monitor_cache_t>
          monitors;
        {
            scoped_lock_t lock (_sync);
            monitors = _service_attachment_state.monitor_cache;
        }
        size_t ready_index = 0;
        if (!wait_for_service_socket_event_local (
              *monitors, ZLINK_POLLIN, flags_, &ready_index,
              [] (const service_monitor_handle_t &entry_) -> socket_base_t * {
                  return static_cast<socket_base_t *> (entry_.handle);
              })) {
            if ((flags_ & ZLINK_DONTWAIT) != 0)
                return -1;
            continue;
        }
        memset (out_, 0, sizeof (*out_));
        zlink_recv_result_t rc =
          zlink_socket_monitor_recv ((*monitors)[ready_index].handle,
                                     &out_->event,
                                     static_cast<zlink_recv_flags_t> (
                                       ZLINK_DONTWAIT));
        if (rc == ZLINK_RECV_OK) {
            copy_service_name_field_local (out_->service_name,
                                           sizeof (out_->service_name),
                                           (*monitors)[ready_index].service_name);
            out_->role = (*monitors)[ready_index].role;
            return 0;
        }
        if (rc != ZLINK_RECV_NO_DATA)
            return -1;
        if ((flags_ & ZLINK_DONTWAIT) != 0) {
            errno = EAGAIN;
            return -1;
        }
    }
}
}
