/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/discovery/discovery.hpp"

namespace zlink
{
void discovery_t::snapshot_registered_service_updates (
  std::vector<registered_service_t> *services_out_, int64_t *value_out_) const
{
    if (!services_out_ && !value_out_)
        return;

    scoped_lock_t lock (_sync);
    _local_state.snapshot_registration (value_out_);
    if (!services_out_)
        return;

    services_out_->clear ();
    for (std::map<registered_service_key_t, registered_service_t>::const_iterator it =
           _registered_services.begin ();
         it != _registered_services.end (); ++it) {
        services_out_->push_back (it->second);
    }
}

int discovery_t::propagate_registered_service_updates (
  const std::vector<registered_service_t> &services_, int64_t value_)
{
    for (size_t i = 0; i < services_.size (); ++i) {
        if (update_service_attributes (services_[i].endpoint.c_str (), services_[i].weight, value_,
                                       &services_[i].metadata, services_[i].service_role)
            != 0) {
            return -1;
        }
    }
    return 0;
}
}
