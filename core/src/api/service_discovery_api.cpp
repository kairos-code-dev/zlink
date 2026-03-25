/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/monitor_api_internal.hpp"
#include "api/service_api_internal.hpp"

#include "services/discovery/discovery_access.hpp"

void *zlink_discovery_new (void *ctx_,
                           zlink_service_type_t service_type_,
                           const char *service_name_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    if (!service_name_ || service_name_[0] == '\0') {
        errno = EINVAL;
        return NULL;
    }
    if (service_type_ != ZLINK_SERVICE_TYPE_SPOT
        && service_type_ != ZLINK_SERVICE_TYPE_SOCKET) {
        errno = EINVAL;
        return NULL;
    }
    return zlink::discovery_access_t::create (
      static_cast<zlink::ctx_t *> (ctx_), service_type_, service_name_);
}

int zlink_discovery_connect_registry (void *discovery_,
                                      const char *registry_endpoint_)
{
    zlink::discovery_t *discovery =
      zlink::discovery_access_t::from_handle (discovery_);
    return discovery ? zlink::discovery_access_t::connect_registry (
                         discovery, registry_endpoint_)
                     : -1;
}

int zlink_discovery_set_value (void *discovery_, int64_t value_)
{
    zlink::discovery_t *discovery =
      zlink::discovery_access_t::from_handle (discovery_);
    return discovery ? zlink::discovery_access_t::set_value (discovery, value_)
                     : -1;
}

int zlink_discovery_get_value (void *discovery_, int64_t *value_out_)
{
    zlink::discovery_t *discovery =
      zlink::discovery_access_t::from_handle (discovery_);
    return discovery
             ? zlink::discovery_access_t::get_value (discovery, value_out_)
             : -1;
}

int zlink_discovery_set_metadata (void *discovery_,
                                  const void *data_,
                                  size_t size_)
{
    zlink::discovery_t *discovery =
      zlink::discovery_access_t::from_handle (discovery_);
    return discovery
             ? zlink::discovery_access_t::set_metadata (discovery, data_, size_)
             : -1;
}

int zlink_discovery_get_metadata (void *discovery_, zlink_msg_t *metadata_out_)
{
    zlink::discovery_t *discovery =
      zlink::discovery_access_t::from_handle (discovery_);
    return discovery
             ? zlink::discovery_access_t::get_metadata (discovery,
                                                        metadata_out_)
             : -1;
}

int zlink_discovery_member_peers (void *discovery_,
                                  zlink_member_peer_entry_t *entries_,
                                  size_t *count_)
{
    zlink::discovery_t *discovery =
      zlink::discovery_access_t::from_handle (discovery_);
    return discovery
             ? zlink::discovery_access_t::member_peers (discovery, entries_,
                                                        count_)
             : -1;
}

int zlink_discovery_member_peer_metadata (void *discovery_,
                                          uint16_t service_role_,
                                          const char *endpoint_,
                                          zlink_msg_t *metadata_out_)
{
    zlink::discovery_t *discovery =
      zlink::discovery_access_t::from_handle (discovery_);
    return discovery
             ? zlink::discovery_access_t::member_peer_metadata (
                 discovery, service_role_, endpoint_, metadata_out_)
             : -1;
}

int zlink_discovery_set_tls_client (void *discovery_,
                                    const char *ca_cert_,
                                    const char *hostname_,
                                    int trust_system_)
{
    zlink::discovery_t *discovery =
      zlink::discovery_access_t::from_handle (discovery_);
    return discovery ? zlink::discovery_access_t::set_tls_client (
                         discovery, ca_cert_, hostname_, trust_system_)
                     : -1;
}

int zlink_discovery_set_routing_id (void *discovery_,
                                    const void *data_,
                                    size_t size_)
{
    zlink::discovery_t *discovery =
      zlink::discovery_access_t::from_handle (discovery_);
    return discovery ? zlink::discovery_access_t::set_routing_id (
                         discovery, data_, size_)
                     : -1;
}

int zlink_discovery_routing_id (void *discovery_, zlink_routing_id_t *out_)
{
    zlink::discovery_t *discovery =
      zlink::discovery_access_t::from_handle (discovery_);
    return discovery ? zlink::discovery_access_t::routing_id (discovery, out_)
                     : -1;
}

int zlink_discovery_destroy (void **discovery_p_)
{
    if (!discovery_p_ || !*discovery_p_) {
        errno = EFAULT;
        return -1;
    }
    zlink::discovery_t *discovery =
      zlink::discovery_access_t::from_handle (*discovery_p_);
    if (!discovery)
        return -1;
    if (has_open_service_monitor_for_subject (discovery)) {
        errno = EBUSY;
        return -1;
    }
    if (zlink::discovery_access_t::destroy (discovery) != 0)
        return -1;
    delete discovery;
    *discovery_p_ = NULL;
    return 0;
}
