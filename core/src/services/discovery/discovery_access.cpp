/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/discovery/discovery_access.hpp"

#include <new>

#include "api/service_handle_internal.hpp"
#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_protocol.hpp"

namespace zlink
{
void *discovery_access_t::create (ctx_t *ctx_,
                                  zlink_service_type_t service_type_,
                                  const char *service_name_)
{
    uint16_t internal_service_type = 0;
    if (service_type_ == ZLINK_SERVICE_TYPE_SPOT)
        internal_service_type = discovery_protocol::service_type_spot_node;
    else if (service_type_ == ZLINK_SERVICE_TYPE_SOCKET)
        internal_service_type = discovery_protocol::service_type_socket;
    else {
        errno = EINVAL;
        return NULL;
    }

    discovery_t *discovery = new (std::nothrow)
      discovery_t (ctx_, internal_service_type, service_name_);
    if (!discovery) {
        errno = ENOMEM;
        return NULL;
    }
    return discovery;
}

discovery_t *discovery_access_t::from_handle (void *discovery_)
{
    if (!discovery_ || !is_registered_discovery_handle (discovery_)) {
        errno = EFAULT;
        return NULL;
    }

    discovery_t *discovery = static_cast<discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return discovery;
}

int discovery_access_t::connect_registry (discovery_t *discovery_,
                                          const char *registry_endpoint_)
{
    return discovery_ ? discovery_->connect_registry (registry_endpoint_) : -1;
}

int discovery_access_t::set_dealer_peer_mode (
  discovery_t *discovery_, zlink_discovery_dealer_peer_mode_t mode_)
{
    return discovery_ ? discovery_->set_dealer_peer_mode (mode_) : -1;
}

int discovery_access_t::set_tls_client (discovery_t *discovery_,
                                        const char *ca_cert_,
                                        const char *hostname_,
                                        int trust_system_)
{
    return discovery_
             ? discovery_->set_tls_client (ca_cert_, hostname_, trust_system_)
             : -1;
}

int discovery_access_t::set_option (discovery_t *discovery_,
                                    int option_,
                                    const void *optval_,
                                    size_t optvallen_)
{
    return discovery_ ? discovery_->set_option (option_, optval_, optvallen_)
                      : -1;
}

int discovery_access_t::get_option (discovery_t *discovery_,
                                    int option_,
                                    void *optval_,
                                    size_t *optvallen_)
{
    return discovery_ ? discovery_->get_option (option_, optval_, optvallen_)
                      : -1;
}

int discovery_access_t::set_routing_id (discovery_t *discovery_,
                                        const void *data_,
                                        size_t size_)
{
    return discovery_ ? discovery_->set_routing_id (data_, size_) : -1;
}

int discovery_access_t::routing_id (discovery_t *discovery_,
                                    zlink_routing_id_t *out_)
{
    return discovery_ ? discovery_->routing_id (out_) : -1;
}

int discovery_access_t::set_value (discovery_t *discovery_, int64_t value_)
{
    return discovery_ ? discovery_->set_value (value_) : -1;
}

int discovery_access_t::get_value (discovery_t *discovery_, int64_t *value_out_)
{
    return discovery_ ? discovery_->get_value (value_out_) : -1;
}

int discovery_access_t::set_metadata (discovery_t *discovery_,
                                      const void *data_,
                                      size_t size_)
{
    return discovery_ ? discovery_->set_metadata (data_, size_) : -1;
}

int discovery_access_t::get_metadata (discovery_t *discovery_,
                                      zlink_msg_t *metadata_out_)
{
    return discovery_ ? discovery_->get_metadata (metadata_out_) : -1;
}

int discovery_access_t::resolve_spot (discovery_t *discovery_,
                                      const zlink_routing_id_t *spot_rid_,
                                      zlink_routing_id_t *owner_node_rid_out_)
{
    return discovery_
             ? discovery_->resolve_spot (spot_rid_, owner_node_rid_out_)
             : -1;
}

int discovery_access_t::member_peers (discovery_t *discovery_,
                                      zlink_member_peer_entry_t *entries_,
                                      size_t *count_)
{
    return discovery_ ? discovery_->member_peers (entries_, count_) : -1;
}

int discovery_access_t::member_peer_metadata (discovery_t *discovery_,
                                              uint16_t service_role_,
                                              const char *endpoint_,
                                              zlink_msg_t *metadata_out_)
{
    return discovery_
             ? discovery_->member_peer_metadata (service_role_, endpoint_,
                                                 metadata_out_)
             : -1;
}

int discovery_access_t::destroy (discovery_t *discovery_)
{
    return discovery_ ? discovery_->destroy () : -1;
}

void discovery_access_t::delete_handle (discovery_t *discovery_)
{
    erase_discovery_handle (discovery_);
    delete discovery_;
}

void *discovery_access_t::monitor_open (discovery_t *discovery_, int events_)
{
    return discovery_ ? discovery_->monitor_open (events_) : NULL;
}

void discovery_access_t::set_summary_enabled (discovery_t *discovery_,
                                              bool enabled_)
{
    if (discovery_)
        discovery_->set_discovery_summary_enabled (enabled_);
}

int discovery_access_t::add_observer (discovery_t *discovery_,
                                      discovery_observer_t *observer_)
{
    return discovery_ ? discovery_->add_observer (observer_) : -1;
}

int discovery_access_t::remove_observer (discovery_t *discovery_,
                                         discovery_observer_t *observer_)
{
    return discovery_ ? discovery_->remove_observer (observer_) : 0;
}

void discovery_access_t::upsert_service_summary (
  discovery_t *discovery_, const zlink_registry_topology_entry_t &entry_)
{
    if (discovery_)
        discovery_->upsert_service_summary (entry_);
}

void discovery_access_t::flush_topology_reports (discovery_t *discovery_)
{
    if (discovery_)
        discovery_->flush_topology_reports ();
}
}
