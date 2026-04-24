/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/discovery/socket_discovery_attachment.hpp"

#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_owned_service.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/discovery/routing_id_utils.hpp"
#include "sockets/socket_base.hpp"

namespace zlink
{
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
}
