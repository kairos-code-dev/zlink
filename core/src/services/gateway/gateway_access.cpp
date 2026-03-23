/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/gateway/gateway_access.hpp"

#include "services/gateway/gateway.hpp"

namespace zlink
{
socket_base_t *gateway_access_t::router_socket (gateway_t *gateway_)
{
    return gateway_ ? static_cast<socket_base_t *> (gateway_->router ()) : NULL;
}

void gateway_access_t::dispatch_send_ready (gateway_t *gateway_)
{
    if (gateway_)
        gateway_->dispatch_send_ready ();
}

void gateway_access_t::dispatch_message (gateway_t *gateway_,
                                         const zlink_routing_id_t *source_rid_,
                                         zlink_msg_t *parts_,
                                         size_t part_count_)
{
    if (gateway_)
        gateway_->dispatch_message (source_rid_, parts_, part_count_);
}
}
