/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/discovery/socket_discovery_attachment.hpp"

#include "services/discovery/discovery_access.hpp"
#include "services/discovery/discovery_owned_service.hpp"
#include "services/discovery/discovery_protocol.hpp"

namespace zlink
{
void socket_discovery_attachment_t::on_local_peer_weight_changed ()
{
    discovery_t *discovery = NULL;
    uint16_t local_role = discovery_protocol::service_role_invalid;
    std::string advertise_endpoint;
    bool registered = false;
    {
        scoped_lock_t lock (_sync);
        discovery = _discovery;
        local_role = _local_role;
        advertise_endpoint = _advertise_endpoint;
        registered = _registered;
    }

    if (!registered || !discovery || advertise_endpoint.empty ())
        return;

    (void) discovery_owned_service::update_attributes (
      discovery, advertise_endpoint.c_str (), local_role,
      _socket->local_peer_weight ());
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
        _discovery_managed_peers_by_rid.clear ();
        _active_peers_by_rid.clear ();
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
        _discovery_managed_peers_by_rid.clear ();
        _active_peers_by_rid.clear ();
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
    _socket->socket_bound_endpoints (&bound_endpoints);
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

int socket_discovery_attachment_t::on_public_disconnect_rid () const
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
        if (!_discovery || _discovery->channel_name () != service_name_)
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
        _discovery_managed_peers_by_rid.clear ();
        _active_peers_by_rid.clear ();
    }

    if (registered && discovery && !advertise_endpoint.empty ()) {
        report_topology (discovery, local_role, advertise_endpoint,
                         ZLINK_TOPOLOGY_STATE_STOPPED, 0, 0, 0, true);
        (void) discovery_owned_service::unregister_endpoint (
          discovery, advertise_endpoint.c_str (), local_role);
    }

    for (std::set<std::string>::const_iterator it = active_endpoints.begin ();
         it != active_endpoints.end (); ++it)
        (void) _socket->service_attachment_term_endpoint (it->c_str ());
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
        _discovery_managed_peers_by_rid.clear ();
        _active_peers_by_rid.clear ();
    }

    (void) zlink_close (static_cast<void *> (_socket));
}
}
