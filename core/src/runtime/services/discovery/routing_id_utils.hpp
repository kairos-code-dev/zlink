/* SPDX-License-Identifier: MPL-2.0 */
#ifndef __ZLINK_DISCOVERY_ROUTING_ID_UTILS_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_ROUTING_ID_UTILS_HPP_INCLUDED__

#include "sockets/common/socket_base.hpp"
#include "utils/routing_id.hpp"

#include <cstring>
#include <string>

namespace zlink
{
namespace discovery
{
inline bool set_socket_routing_id (socket_base_t *socket_,
                                   const std::string *override_id_,
                                   zlink_routing_id_t *out_)
{
    if (!socket_)
        return false;
    if (override_id_ && !override_id_->empty ()) {
        if (socket_->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, override_id_->data (),
                                 override_id_->size ())
            != 0)
            return false;
    } else {
        zlink_routing_id_t rid;
        generate_random_uuid_routing_id (&rid);
        if (socket_->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, rid.data, rid.size) != 0)
            return false;
    }
    if (out_) {
        size_t size = sizeof (out_->data);
        if (socket_->getsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, out_->data, &size) != 0)
            return false;
        out_->size = static_cast<uint8_t> (size);
    }
    return true;
}

inline bool ensure_socket_routing_id_present (socket_base_t *socket_,
                                              const std::string *override_id_,
                                              zlink_routing_id_t *out_)
{
    if (!socket_)
        return false;
    if (override_id_ && !override_id_->empty ())
        return set_socket_routing_id (socket_, override_id_, out_);

    unsigned char buf[256];
    size_t size = sizeof (buf);
    if (socket_->getsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, buf, &size) != 0)
        return false;
    if (size > 0) {
        if (out_) {
            out_->size = static_cast<uint8_t> (size);
            memcpy (out_->data, buf, size);
        }
        return true;
    }

    return set_socket_routing_id (socket_, override_id_, out_);
}
} // namespace discovery
} // namespace zlink

#endif
