/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SERVICE_SPOT_ROUTED_PROTOCOL_INTERNAL_HPP_INCLUDED__
#define __ZLINK_SERVICE_SPOT_ROUTED_PROTOCOL_INTERNAL_HPP_INCLUDED__

#include <stdint.h>

namespace zlink
{
namespace spot_routed_protocol
{
enum : uint8_t
{
    protocol_id = 0x02,
    legacy_frame_version = 0x01,
    packed_frame_version = 0x02,
    spot_endpoint_class = 0x01,
    router_endpoint_class = 0x02,
    actor_gateway_endpoint_class = 0x03
};

inline bool is_endpoint_class (uint8_t value_)
{
    return value_ == spot_endpoint_class || value_ == router_endpoint_class
           || value_ == actor_gateway_endpoint_class;
}
}
}

#endif
