/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/discovery/socket_discovery_attachment.hpp"

#include "services/discovery/discovery_owned_service.hpp"
#include "sockets/socket_base.hpp"
#include "services/discovery/discovery_access.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/discovery/routing_id_utils.hpp"

namespace zlink
{
namespace
{
static int compare_routing_id_bytes (const zlink_routing_id_t &lhs_,
                                     const zlink_routing_id_t &rhs_)
{
    const size_t lhs_size = lhs_.size;
    const size_t rhs_size = rhs_.size;
    const size_t common = lhs_size < rhs_size ? lhs_size : rhs_size;
    if (common > 0) {
        const int cmp = memcmp (lhs_.data, rhs_.data, common);
        if (cmp != 0)
            return cmp;
    }
    if (lhs_size < rhs_size)
        return -1;
    if (lhs_size > rhs_size)
        return 1;
    return 0;
}

static int compare_connect_keys (const zlink_routing_id_t &local_rid_,
                                 const zlink_routing_id_t &remote_rid_,
                                 const std::string &local_endpoint_,
                                 const std::string &remote_endpoint_)
{
    if (local_rid_.size > 0 && remote_rid_.size > 0) {
        const int rid_cmp = compare_routing_id_bytes (local_rid_, remote_rid_);
        if (rid_cmp != 0)
            return rid_cmp;
    }

    if (local_endpoint_ < remote_endpoint_)
        return -1;
    if (local_endpoint_ > remote_endpoint_)
        return 1;
    return 0;
}

static bool should_initiate_router_router (
  const zlink_routing_id_t &local_rid_,
  const zlink_routing_id_t &remote_rid_,
  const std::string &local_endpoint_,
  const std::string &remote_endpoint_)
{
    return compare_connect_keys (local_rid_, remote_rid_, local_endpoint_,
                                 remote_endpoint_)
           < 0;
}
}

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

socket_discovery_attachment_t::~socket_discovery_attachment_t () {}

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
    _socket->socket_bound_endpoints (&bound_endpoints);
    if (bound_endpoints.size () > 1) {
        errno = EBUSY;
        return -1;
    }
    if (_socket->socket_has_manual_connect_endpoints ()
        || _socket->socket_has_attached_pipes ()) {
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

    zlink_routing_id_t routing_id;
    memset (&routing_id, 0, sizeof (routing_id));
    const zlink_routing_id_t *routing_id_ptr = NULL;
    if ((local_role_ == discovery_protocol::service_role_router
         || local_role_ == discovery_protocol::service_role_dealer)
        && ensure_socket_routing_id (&routing_id)) {
        routing_id_ptr = &routing_id;
    }

    return discovery_owned_service::register_endpoint (
      discovery_, discovery_protocol::service_type_socket, endpoint_.c_str (),
      resolved_endpoint_out_, routing_id_ptr, local_role_,
      _socket->local_admission_state ());
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

    zlink_routing_id_t local_routing_id;
    memset (&local_routing_id, 0, sizeof (local_routing_id));
    if (local_role_ == discovery_protocol::service_role_router
        && !ensure_socket_routing_id (&local_routing_id)) {
        return;
    }

    std::vector<provider_info_t> providers;
    const zlink_discovery_dealer_peer_mode_t dealer_peer_mode =
      discovery_->dealer_peer_mode ();
    discovery_->snapshot_providers (discovery_->service_name (), &providers);

    std::set<std::string> target_endpoints;
    for (size_t i = 0; i < providers.size (); ++i) {
        const provider_info_t &provider = providers[i];
        if (provider.endpoint.empty ()
            || advertise_endpoint_ == provider.endpoint
            || !discovery_protocol::socket_auto_connect_target_matches (
              local_role_, provider.service_role, dealer_peer_mode)) {
            continue;
        }
        if (local_role_ == discovery_protocol::service_role_router
            && provider.service_role
                 == discovery_protocol::service_role_router
            && !should_initiate_router_router (local_routing_id,
                                              provider.routing_id,
                                              advertise_endpoint_,
                                              provider.endpoint)) {
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
        if (_socket->service_attachment_connect (it->c_str ()) == 0) {
            scoped_lock_t lock (_sync);
            if (_discovery == discovery_ && !_shutdown_requested)
                _active_peer_endpoints.insert (*it);
        }
    }

    for (std::set<std::string>::const_iterator it = active_endpoints.begin ();
         it != active_endpoints.end (); ++it) {
        if (target_endpoints.find (*it) != target_endpoints.end ())
            continue;
        if (_socket->service_attachment_term_endpoint (it->c_str ()) == 0) {
            scoped_lock_t lock (_sync);
            _active_peer_endpoints.erase (*it);
        }
    }

    if (!advertise_endpoint_.empty ()) {
        size_t ready_count = 0;
        {
            scoped_lock_t lock (_sync);
            if (_discovery != discovery_)
                return;
            ready_count = _active_peer_endpoints.size ();
        }
        report_topology (discovery_, local_role_, advertise_endpoint_,
                         ZLINK_TOPOLOGY_STATE_READY,
                         static_cast<uint32_t> (target_endpoints.size ()),
                         static_cast<uint32_t> (ready_count),
                         0, false);
    }
}

}
