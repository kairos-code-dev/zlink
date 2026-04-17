/* SPDX-License-Identifier: MPL-2.0 */
#ifndef __ZLINK_DISCOVERY_ROUTING_ID_UTILS_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_ROUTING_ID_UTILS_HPP_INCLUDED__

#include "protocol/wire.hpp"
#include "api/routing_id_internal.hpp"
#include "sockets/socket_base.hpp"
#include "utils/random.hpp"

#include <cstring>
#include <string>

namespace zlink
{
namespace discovery
{
static thread_local uint8_t g_discovery_socket_routing_id_storage[255];

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
        unsigned char buf[5];
        buf[0] = 0;
        uint32_t rid = generate_random ();
        if (rid == 0)
            rid = 1;
        put_uint32 (buf + 1, rid);
        if (socket_->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, buf, sizeof buf) != 0)
            return false;
    }
    if (out_) {
        size_t size = sizeof (g_discovery_socket_routing_id_storage);
        if (socket_->getsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID,
                                 g_discovery_socket_routing_id_storage, &size)
            != 0)
            return false;
        if (!zlink::routing_id_internal::assign_view (
              out_,
              size > 0 ? g_discovery_socket_routing_id_storage : NULL, size))
            return false;
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
            memcpy (g_discovery_socket_routing_id_storage, buf, size);
            if (!zlink::routing_id_internal::assign_view (
                  out_, g_discovery_socket_routing_id_storage, size))
                return false;
        }
        return true;
    }

    return set_socket_routing_id (socket_, override_id_, out_);
}
} // namespace discovery
} // namespace zlink

#endif
