/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SERVICES_DISCOVERY_DISCOVERY_OWNED_SERVICE_HPP_INCLUDED__
#define __ZLINK_SERVICES_DISCOVERY_DISCOVERY_OWNED_SERVICE_HPP_INCLUDED__

#include "services/discovery/discovery.hpp"

#include <cerrno>
#include <string>

namespace zlink
{
namespace discovery_owned_service
{
inline int register_endpoint (discovery_t *discovery_,
                              uint16_t service_type_,
                              const char *endpoint_,
                              uint32_t weight_,
                              std::string *resolved_endpoint_out_,
                              const zlink_routing_id_t *routing_id_ = NULL,
                              uint16_t service_role_ = 0)
{
    if (!discovery_ || !endpoint_ || endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    const std::string &service_name = discovery_->service_name ();
    if (service_name.empty ()) {
        errno = EINVAL;
        return -1;
    }

    return discovery_->register_service (
      service_type_, service_name.c_str (), endpoint_, weight_,
      resolved_endpoint_out_, routing_id_, service_role_);
}

inline int unregister_endpoint (discovery_t *discovery_,
                                uint16_t service_type_,
                                const char *endpoint_,
                                uint16_t service_role_ = 0)
{
    if (!discovery_ || !endpoint_ || endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    const std::string &service_name = discovery_->service_name ();
    if (service_name.empty ()) {
        errno = EINVAL;
        return -1;
    }

    return discovery_->unregister_service (
      service_type_, service_name.c_str (), endpoint_, service_role_);
}

inline int update_weight (discovery_t *discovery_,
                          uint16_t service_type_,
                          const char *endpoint_,
                          uint32_t weight_,
                          uint16_t service_role_ = 0)
{
    if (!discovery_ || !endpoint_ || endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    const std::string &service_name = discovery_->service_name ();
    if (service_name.empty ()) {
        errno = EINVAL;
        return -1;
    }

    return discovery_->update_service_weight (
      service_type_, service_name.c_str (), endpoint_, weight_, service_role_);
}
}
}

#endif
