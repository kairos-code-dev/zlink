/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/discovery/discovery_access.hpp"

#include "services/discovery/discovery.hpp"

namespace zlink
{
void discovery_access_t::set_summary_enabled (discovery_t *discovery_,
                                              bool enabled_)
{
    if (discovery_)
        discovery_->set_discovery_summary_enabled (enabled_);
}

void discovery_access_t::add_observer (discovery_t *discovery_,
                                       discovery_observer_t *observer_)
{
    if (discovery_)
        discovery_->add_observer (observer_);
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

void discovery_access_t::upsert_gateway_peer_summary (
  discovery_t *discovery_, const zlink_registry_gateway_peer_entry_t &entry_)
{
    if (discovery_)
        discovery_->upsert_gateway_peer_summary (entry_);
}

void discovery_access_t::erase_service_summary (
  discovery_t *discovery_,
  uint16_t service_kind_,
  const zlink_routing_id_t &routing_id_,
  const std::string &service_name_,
  bool stopped_)
{
    if (discovery_) {
        discovery_->erase_service_summary (
          service_kind_, routing_id_, service_name_, stopped_);
    }
}
}
