/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/discovery/discovery.hpp"

namespace zlink
{
void discovery_t::snapshot_registered_service_updates (
  std::vector<registered_service_t> *services_out_,
  int64_t *value_out_,
  std::vector<unsigned char> *metadata_out_) const
{
    if (!services_out_ && !value_out_ && !metadata_out_)
        return;

    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    _local_state.snapshot_registration (value_out_, metadata_out_);
    if (!services_out_)
        return;

    services_out_->clear ();
    for (std::map<registered_service_key_t, registered_service_t>::const_iterator
           it = _registered_services.begin ();
         it != _registered_services.end (); ++it) {
        services_out_->push_back (it->second);
    }
}

int discovery_t::propagate_registered_service_updates (
  const std::vector<registered_service_t> &services_,
  int64_t value_,
  const std::vector<unsigned char> &metadata_)
{
    for (size_t i = 0; i < services_.size (); ++i) {
        if (update_service_attributes (services_[i].service_type,
                                       services_[i].service_name.c_str (),
                                       services_[i].endpoint.c_str (),
                                       services_[i].admission_state, value_,
                                       &metadata_, services_[i].service_role)
            != 0) {
            return -1;
        }
    }
    return 0;
}
}
