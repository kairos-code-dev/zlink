/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_GATEWAY_ACCESS_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_GATEWAY_ACCESS_HPP_INCLUDED__

#include <zlink.h>

namespace zlink
{
class gateway_t;
class socket_base_t;

struct gateway_access_t
{
    static socket_base_t *router_socket (gateway_t *gateway_);
    static void dispatch_send_ready (gateway_t *gateway_);
    static void dispatch_message (gateway_t *gateway_,
                                  const zlink_routing_id_t *source_rid_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_);
};
}

#endif
