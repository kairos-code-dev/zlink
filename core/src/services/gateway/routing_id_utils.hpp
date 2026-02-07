/* SPDX-License-Identifier: MPL-2.0 */
#ifndef __ZLINK_DISCOVERY_ROUTING_ID_UTILS_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_ROUTING_ID_UTILS_HPP_INCLUDED__

#include "protocol/wire.hpp"
#include "sockets/socket_base.hpp"
#include "utils/random.hpp"

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
        if (socket_->setsockopt (ZLINK_ROUTING_ID, override_id_->data (),
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
        if (socket_->setsockopt (ZLINK_ROUTING_ID, buf, sizeof buf) != 0)
            return false;
    }
    if (out_) {
        size_t size = sizeof (out_->data);
        if (socket_->getsockopt (ZLINK_ROUTING_ID, out_->data, &size) != 0)
            return false;
        out_->size = static_cast<uint8_t> (size);
    }
    return true;
}
} // namespace discovery
} // namespace zlink

#endif
