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

    int64_t value = 0;
    if (discovery_->get_value (&value) != 0)
        return -1;

    zlink_msg_t metadata_msg;
    if (discovery_->get_metadata (&metadata_msg) != 0)
        return -1;
    std::vector<unsigned char> metadata;
    if (zlink_msg_size (&metadata_msg) > 0) {
        const unsigned char *data =
          static_cast<const unsigned char *> (zlink_msg_data (&metadata_msg));
        metadata.assign (data, data + zlink_msg_size (&metadata_msg));
    }
    zlink_msg_close (&metadata_msg);

    return discovery_->register_service (
      service_type_, service_name.c_str (), endpoint_, value, &metadata,
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

inline int update_attributes (discovery_t *discovery_,
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

    int64_t value = 0;
    if (discovery_->get_value (&value) != 0)
        return -1;

    zlink_msg_t metadata_msg;
    if (discovery_->get_metadata (&metadata_msg) != 0)
        return -1;
    std::vector<unsigned char> metadata;
    if (zlink_msg_size (&metadata_msg) > 0) {
        const unsigned char *data =
          static_cast<const unsigned char *> (zlink_msg_data (&metadata_msg));
        metadata.assign (data, data + zlink_msg_size (&metadata_msg));
    }
    zlink_msg_close (&metadata_msg);

    return discovery_->update_service_attributes (
      service_type_, service_name.c_str (), endpoint_, value, &metadata,
      service_role_);
}
}
}

#endif
