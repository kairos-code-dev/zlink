/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <new>

#include "services/discovery/registry_access.hpp"

#include "services/discovery/registry.hpp"

namespace zlink
{
void *registry_access_t::create (ctx_t *ctx_)
{
    registry_t *registry = new (std::nothrow) registry_t (ctx_);
    if (!registry) {
        errno = ENOMEM;
        return NULL;
    }
    return registry;
}

registry_t *registry_access_t::from_handle (void *registry_)
{
    if (!registry_) {
        errno = EFAULT;
        return NULL;
    }

    registry_t *registry = static_cast<registry_t *> (registry_);
    if (!registry->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return registry;
}

int registry_access_t::bind (registry_t *registry_,
                             const char *pub_endpoint_,
                             const char *router_endpoint_)
{
    return registry_ ? registry_->bind (pub_endpoint_, router_endpoint_) : -1;
}

int registry_access_t::set_id (registry_t *registry_, uint32_t registry_id_)
{
    return registry_ ? registry_->set_id (registry_id_) : -1;
}

int registry_access_t::add_peer (registry_t *registry_,
                                 const char *peer_pub_endpoint_)
{
    return registry_ ? registry_->add_peer (peer_pub_endpoint_) : -1;
}

int registry_access_t::set_heartbeat (registry_t *registry_,
                                      uint32_t interval_ms_,
                                      uint32_t timeout_ms_)
{
    return registry_ ? registry_->set_heartbeat (interval_ms_, timeout_ms_)
                     : -1;
}

int registry_access_t::set_broadcast_interval (registry_t *registry_,
                                               uint32_t interval_ms_)
{
    return registry_ ? registry_->set_broadcast_interval (interval_ms_) : -1;
}

int registry_access_t::destroy (registry_t *registry_)
{
    return registry_ ? registry_->destroy () : -1;
}

void registry_access_t::delete_handle (registry_t *registry_)
{
    delete registry_;
}

int registry_access_t::set_tls_server (registry_t *registry_,
                                       const char *cert_,
                                       const char *key_,
                                       int require_client_cert_)
{
    return registry_
             ? registry_->set_tls_server (cert_, key_, require_client_cert_)
             : -1;
}

int registry_access_t::set_tls_client (registry_t *registry_,
                                       const char *ca_cert_,
                                       const char *hostname_,
                                       int trust_system_)
{
    return registry_
             ? registry_->set_tls_client (ca_cert_, hostname_, trust_system_)
             : -1;
}

int registry_access_t::topology_snapshot (registry_t *registry_,
                                          zlink_registry_topology_entry_t *entries_,
                                          size_t *count_)
{
    return registry_ ? registry_->topology_snapshot (entries_, count_) : -1;
}

int registry_access_t::status_snapshot (registry_t *registry_,
                                        zlink_registry_status_t *out_)
{
    return registry_ ? registry_->status_snapshot (out_) : -1;
}

int registry_access_t::service_summary_snapshot (
  registry_t *registry_,
  const zlink_registry_service_summary_filter_t *filter_,
  std::vector<zlink_registry_service_summary_entry_t> *out_)
{
    return registry_ ? registry_->service_summary_snapshot (filter_, out_) : -1;
}

int registry_access_t::topology_query (
  registry_t *registry_,
  const zlink_registry_topology_filter_t *filter_,
  zlink_registry_topology_entry_t *entries_,
  size_t *count_)
{
    return registry_ ? registry_->topology_query (filter_, entries_, count_)
                     : -1;
}
}
