/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/discovery/socket_discovery_attachment.hpp"

#include "services/discovery/discovery_owned_service.hpp"
#include "sockets/socket_base.hpp"
#include "services/discovery/discovery_access.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/gateway/routing_id_utils.hpp"

namespace zlink
{
socket_discovery_attachment_t::socket_discovery_attachment_t (
  socket_base_t *socket_) :
    _socket (socket_),
    _discovery (NULL),
    _local_role (discovery_protocol::service_role_invalid),
    _registered (false),
    _shutdown_requested (false),
    _refresh_seq (0)
{
}

socket_discovery_attachment_t::~socket_discovery_attachment_t ()
{
}

void socket_discovery_attachment_t::collect_bound_endpoints (
  std::set<std::string> *out_) const
{
    if (!out_)
        return;
    out_->clear ();
    const socket_base_t::endpoints_t &endpoints = _socket->endpoint_runtime ().endpoints;
    for (socket_base_t::endpoints_t::const_iterator it = endpoints.begin ();
         it != endpoints.end (); ++it) {
        if (it->second.local_type == endpoint_type_bind)
            out_->insert (it->first);
    }
}

bool socket_discovery_attachment_t::has_manual_connect_endpoints () const
{
    const socket_base_t::endpoints_t &endpoints = _socket->endpoint_runtime ().endpoints;
    for (socket_base_t::endpoints_t::const_iterator it = endpoints.begin ();
         it != endpoints.end (); ++it) {
        if (it->second.local_type == endpoint_type_connect)
            return true;
    }
    return false;
}

bool socket_discovery_attachment_t::public_api_forbidden (int *errno_out_) const
{
    scoped_lock_t lock (_sync);
    if (!_discovery)
        return false;
    if (errno_out_)
        *errno_out_ = _shutdown_requested ? ESHUTDOWN : EFSM;
    return true;
}

int socket_discovery_attachment_t::attach_validation (
  discovery_t *discovery_,
  uint16_t *local_role_out_,
  std::string *bound_endpoint_out_)
{
    if (!discovery_
        || discovery_->service_type () != discovery_protocol::service_type_socket) {
        errno = EINVAL;
        return -1;
    }

    const uint16_t local_role =
      discovery_protocol::derive_socket_service_role (_socket->socket_type ());
    if (!discovery_protocol::is_valid_service_role_for_type (
          discovery_protocol::service_type_socket, local_role)) {
        errno = ENOTSUP;
        return -1;
    }

    std::set<std::string> bound_endpoints;
    collect_bound_endpoints (&bound_endpoints);
    if (bound_endpoints.size () > 1) {
        errno = EBUSY;
        return -1;
    }
    if (has_manual_connect_endpoints () || _socket->has_attached_pipes ()) {
        errno = EBUSY;
        return -1;
    }

    if (local_role_out_)
        *local_role_out_ = local_role;
    if (bound_endpoint_out_) {
        bound_endpoint_out_->clear ();
        if (!bound_endpoints.empty ())
            *bound_endpoint_out_ = *bound_endpoints.begin ();
    }
    return 0;
}

int socket_discovery_attachment_t::register_bound_endpoint (
  discovery_t *discovery_,
  uint16_t local_role_,
  const std::string &endpoint_,
  std::string *resolved_endpoint_out_)
{
    if (!discovery_ || endpoint_.empty ()) {
        errno = EINVAL;
        return -1;
    }

    return discovery_owned_service::register_endpoint (
      discovery_, discovery_protocol::service_type_socket, endpoint_.c_str (),
      1, resolved_endpoint_out_, NULL, local_role_);
}

bool socket_discovery_attachment_t::ensure_socket_routing_id (
  zlink_routing_id_t *out_)
{
    if (!out_) {
        errno = EINVAL;
        return false;
    }
    memset (out_, 0, sizeof (*out_));
    return zlink::discovery::ensure_socket_routing_id_present (_socket, NULL,
                                                               out_);
}

void socket_discovery_attachment_t::report_topology (
  discovery_t *discovery_,
  uint16_t local_role_,
  const std::string &advertise_endpoint_,
  uint16_t state_,
  uint32_t desired_count_,
  uint32_t ready_count_,
  int error_code_,
  bool flush_immediately_)
{
    if (!discovery_ || advertise_endpoint_.empty ())
        return;

    zlink_routing_id_t routing_id;
    if (!ensure_socket_routing_id (&routing_id))
        return;

    zlink_registry_topology_entry_t entry;
    memset (&entry, 0, sizeof (entry));
    entry.routing_id = routing_id;
    entry.service_kind = ZLINK_SERVICE_KIND_SOCKET;
    entry.service_role = static_cast<zlink_service_role_t> (local_role_);
    strncpy (entry.service_name, discovery_->service_name ().c_str (),
             sizeof (entry.service_name) - 1);
    strncpy (entry.endpoint, advertise_endpoint_.c_str (),
             sizeof (entry.endpoint) - 1);
    entry.source = ZLINK_TOPOLOGY_SOURCE_DISCOVERY;
    entry.state = static_cast<zlink_topology_state_t> (state_);
    entry.desired_count = desired_count_;
    entry.ready_count = ready_count_;
    entry.error_code = static_cast<uint32_t> (error_code_ > 0 ? error_code_ : 0);
    entry.last_reported_ms = zlink::clock_t ().now_ms ();
    discovery_access_t::upsert_service_summary (discovery_, entry);
    if (flush_immediately_)
        discovery_access_t::flush_topology_reports (discovery_);
}

void socket_discovery_attachment_t::refresh_peers (
  discovery_t *discovery_,
  uint16_t local_role_,
  const std::string &advertise_endpoint_,
  bool shutdown_requested_)
{
    if (!discovery_ || shutdown_requested_)
        return;

    std::vector<provider_info_t> providers;
    discovery_->snapshot_providers (discovery_->service_name (), &providers);

    std::set<std::string> target_endpoints;
    for (size_t i = 0; i < providers.size (); ++i) {
        const provider_info_t &provider = providers[i];
        if (provider.endpoint.empty ()
            || provider.endpoint == advertise_endpoint_
            || !discovery_protocol::service_roles_match (
              local_role_, provider.service_role)) {
            continue;
        }
        target_endpoints.insert (provider.endpoint);
    }

    std::set<std::string> active_endpoints;
    {
        scoped_lock_t lock (_sync);
        if (_discovery != discovery_ || _shutdown_requested)
            return;
        active_endpoints = _active_peer_endpoints;
        _discovery_managed_peer_endpoints = target_endpoints;
        _refresh_seq =
          discovery_->service_update_seq (discovery_->service_name ());
    }

    for (std::set<std::string>::const_iterator it = target_endpoints.begin ();
         it != target_endpoints.end (); ++it) {
        if (active_endpoints.find (*it) != active_endpoints.end ())
            continue;
        if (_socket->connect_internal (it->c_str ()) == 0) {
            scoped_lock_t lock (_sync);
            if (_discovery == discovery_ && !_shutdown_requested)
                _active_peer_endpoints.insert (*it);
        }
    }

    for (std::set<std::string>::const_iterator it = active_endpoints.begin ();
         it != active_endpoints.end (); ++it) {
        if (target_endpoints.find (*it) != target_endpoints.end ())
            continue;
        if (_socket->term_endpoint_internal (it->c_str ()) == 0) {
            scoped_lock_t lock (_sync);
            _active_peer_endpoints.erase (*it);
        }
    }

    if (!advertise_endpoint_.empty ()) {
        std::set<std::string> current_active_endpoints;
        {
            scoped_lock_t lock (_sync);
            if (_discovery != discovery_)
                return;
            current_active_endpoints = _active_peer_endpoints;
        }
        report_topology (discovery_, local_role_, advertise_endpoint_,
                         ZLINK_TOPOLOGY_STATE_READY,
                         static_cast<uint32_t> (target_endpoints.size ()),
                         static_cast<uint32_t> (current_active_endpoints.size ()),
                         0, false);
    }
}

int socket_discovery_attachment_t::attach (discovery_t *discovery_)
{
    uint16_t local_role = discovery_protocol::service_role_invalid;
    std::string bound_endpoint;
    if (attach_validation (discovery_, &local_role, &bound_endpoint) != 0)
        return -1;

    {
        scoped_lock_t lock (_sync);
        if (_discovery == discovery_)
            return 0;
        if (_discovery) {
            errno = EBUSY;
            return -1;
        }
    }

    if (discovery_access_t::add_observer (discovery_, this) != 0)
        return -1;

    {
        scoped_lock_t lock (_sync);
        _discovery = discovery_;
        _local_role = local_role;
        _registered = false;
        _shutdown_requested = false;
        _refresh_seq = 0;
        _advertise_endpoint = bound_endpoint;
        _discovery_managed_peer_endpoints.clear ();
        _active_peer_endpoints.clear ();
    }

    std::string advertise_endpoint;
    if (!bound_endpoint.empty ()
        && register_bound_endpoint (discovery_, local_role, bound_endpoint,
                                    &advertise_endpoint)
             != 0) {
        scoped_lock_t lock (_sync);
        _discovery = NULL;
        _local_role = discovery_protocol::service_role_invalid;
        _advertise_endpoint.clear ();
        _registered = false;
        _shutdown_requested = false;
        _refresh_seq = 0;
        _discovery_managed_peer_endpoints.clear ();
        _active_peer_endpoints.clear ();
        (void) discovery_access_t::remove_observer (discovery_, this);
        return -1;
    }

    {
        scoped_lock_t lock (_sync);
        _registered = !bound_endpoint.empty ();
        _advertise_endpoint =
          advertise_endpoint.empty () ? bound_endpoint : advertise_endpoint;
    }

    report_topology (discovery_, local_role,
                     advertise_endpoint.empty () ? bound_endpoint
                                                : advertise_endpoint,
                     ZLINK_TOPOLOGY_STATE_READY, 0, 0, 0, false);
    refresh_peers (discovery_, local_role, advertise_endpoint.empty ()
                                              ? bound_endpoint
                                              : advertise_endpoint,
                   false);
    return 0;
}

int socket_discovery_attachment_t::on_public_bind_begin (const char *endpoint_)
{
    LIBZLINK_UNUSED (endpoint_);

    scoped_lock_t lock (_sync);
    if (!_discovery)
        return 0;
    if (_shutdown_requested) {
        errno = ESHUTDOWN;
        return -1;
    }

    std::set<std::string> bound_endpoints;
    collect_bound_endpoints (&bound_endpoints);
    if (bound_endpoints.size () > 1) {
        errno = EBUSY;
        return -1;
    }
    if (!bound_endpoints.empty ()) {
        errno = EBUSY;
        return -1;
    }
    return 0;
}

int socket_discovery_attachment_t::on_bind_success (const std::string &endpoint_)
{
    discovery_t *discovery = NULL;
    uint16_t local_role = discovery_protocol::service_role_invalid;
    {
        scoped_lock_t lock (_sync);
        if (!_discovery)
            return 0;
        if (_shutdown_requested) {
            errno = ESHUTDOWN;
            return -1;
        }
        if (_registered)
            return 0;
        discovery = _discovery;
        local_role = _local_role;
        _advertise_endpoint = endpoint_;
    }

    std::string resolved_endpoint;
    if (register_bound_endpoint (discovery, local_role, endpoint_,
                                 &resolved_endpoint)
        != 0) {
        scoped_lock_t lock (_sync);
        if (_discovery == discovery && !_registered)
            _advertise_endpoint.clear ();
        return -1;
    }

    {
        scoped_lock_t lock (_sync);
        if (_discovery != discovery || _shutdown_requested)
            return 0;
        _registered = true;
        _advertise_endpoint =
          resolved_endpoint.empty () ? endpoint_ : resolved_endpoint;
    }

    report_topology (discovery, local_role,
                     resolved_endpoint.empty () ? endpoint_ : resolved_endpoint,
                     ZLINK_TOPOLOGY_STATE_READY, 0, 0, 0, false);
    refresh_peers (discovery, local_role,
                   resolved_endpoint.empty () ? endpoint_ : resolved_endpoint,
                   false);
    return 0;
}

int socket_discovery_attachment_t::on_public_connect () const
{
    int err = 0;
    if (!public_api_forbidden (&err))
        return 0;
    errno = err;
    return -1;
}

int socket_discovery_attachment_t::on_public_term_endpoint () const
{
    int err = 0;
    if (!public_api_forbidden (&err))
        return 0;
    errno = err;
    return -1;
}

int socket_discovery_attachment_t::on_public_close () const
{
    int err = 0;
    if (!public_api_forbidden (&err))
        return 0;
    errno = err;
    return -1;
}

void socket_discovery_attachment_t::on_service_update (
  const std::string &service_name_)
{
    discovery_t *discovery = NULL;
    uint16_t local_role = discovery_protocol::service_role_invalid;
    std::string advertise_endpoint;
    bool shutdown_requested = false;
    {
        scoped_lock_t lock (_sync);
        if (!_discovery || _discovery->service_name () != service_name_)
            return;
        discovery = _discovery;
        local_role = _local_role;
        advertise_endpoint = _advertise_endpoint;
        shutdown_requested = _shutdown_requested;
    }
    refresh_peers (discovery, local_role, advertise_endpoint,
                   shutdown_requested);
}

void socket_discovery_attachment_t::on_discovery_shutdown_requested (
  discovery_t *discovery_)
{
    discovery_t *discovery = NULL;
    uint16_t local_role = discovery_protocol::service_role_invalid;
    std::string advertise_endpoint;
    std::set<std::string> active_endpoints;
    bool registered = false;
    {
        scoped_lock_t lock (_sync);
        if (_discovery != discovery_)
            return;
        _shutdown_requested = true;
        discovery = _discovery;
        local_role = _local_role;
        advertise_endpoint = _advertise_endpoint;
        active_endpoints = _active_peer_endpoints;
        registered = _registered;
        _registered = false;
        _discovery_managed_peer_endpoints.clear ();
        _active_peer_endpoints.clear ();
    }

    if (registered && discovery && !advertise_endpoint.empty ()) {
        report_topology (discovery, local_role, advertise_endpoint,
                         ZLINK_TOPOLOGY_STATE_STOPPED, 0, 0, 0, true);
        (void) discovery_owned_service::unregister_endpoint (
          discovery, discovery_protocol::service_type_socket,
          advertise_endpoint.c_str (), local_role);
    }

    for (std::set<std::string>::const_iterator it = active_endpoints.begin ();
         it != active_endpoints.end (); ++it)
        (void) _socket->term_endpoint_internal (it->c_str ());
}

void socket_discovery_attachment_t::on_discovery_destroyed (
  discovery_t *discovery_)
{
    {
        scoped_lock_t lock (_sync);
        if (_discovery != discovery_)
            return;
        _discovery = NULL;
        _local_role = discovery_protocol::service_role_invalid;
        _advertise_endpoint.clear ();
        _registered = false;
        _shutdown_requested = false;
        _refresh_seq = 0;
        _discovery_managed_peer_endpoints.clear ();
        _active_peer_endpoints.clear ();
    }

    (void) zlink_close (static_cast<void *> (_socket));
}
}
