/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/monitor_api_internal.hpp"
#include "api/service_api_internal.hpp"

#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/discovery/registry.hpp"

#include <new>

void *zlink_discovery_new (void *ctx_, zlink_service_type_t service_type_)
{
    if (!ctx_ || !(static_cast<zlink::ctx_t *> (ctx_))->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    if (service_type_ != ZLINK_SERVICE_TYPE_GATEWAY
        && service_type_ != ZLINK_SERVICE_TYPE_SPOT) {
        errno = EINVAL;
        return NULL;
    }
    uint16_t internal_service_type = 0;
    if (service_type_ == ZLINK_SERVICE_TYPE_GATEWAY)
        internal_service_type =
          zlink::discovery_protocol::service_type_gateway_receiver;
    else if (service_type_ == ZLINK_SERVICE_TYPE_SPOT)
        internal_service_type = zlink::discovery_protocol::service_type_spot_node;

    zlink::discovery_t *discovery = new (std::nothrow)
      zlink::discovery_t (static_cast<zlink::ctx_t *> (ctx_),
                          internal_service_type);
    if (!discovery) {
        errno = ENOMEM;
        return NULL;
    }
    return static_cast<void *> (discovery);
}

int zlink_discovery_connect_registry (void *discovery_,
                                      const char *registry_endpoint_)
{
    if (!discovery_)
        return -1;
    zlink::discovery_t *discovery = static_cast<zlink::discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return discovery->connect_registry (registry_endpoint_);
}

int zlink_discovery_set_tls_client (void *discovery_,
                                    const char *ca_cert_,
                                    const char *hostname_,
                                    int trust_system_)
{
    if (!discovery_)
        return -1;
    zlink::discovery_t *discovery =
      static_cast<zlink::discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return discovery->set_tls_client (ca_cert_, hostname_, trust_system_);
}

int zlink_discovery_set_routing_id (void *discovery_,
                                    const void *data_,
                                    size_t size_)
{
    if (!discovery_)
        return -1;
    zlink::discovery_t *discovery =
      static_cast<zlink::discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return discovery->set_routing_id (data_, size_);
}

int zlink_discovery_routing_id (void *discovery_, zlink_routing_id_t *out_)
{
    if (!discovery_)
        return -1;
    zlink::discovery_t *discovery =
      static_cast<zlink::discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return discovery->routing_id (out_);
}

int zlink_discovery_destroy (void **discovery_p_)
{
    if (!discovery_p_ || !*discovery_p_) {
        errno = EFAULT;
        return -1;
    }
    zlink::discovery_t *discovery = static_cast<zlink::discovery_t *> (*discovery_p_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    if (has_open_service_monitor_for_subject (discovery)) {
        errno = EBUSY;
        return -1;
    }
    if (discovery->destroy () != 0)
        return -1;
    delete discovery;
    *discovery_p_ = NULL;
    return 0;
}
