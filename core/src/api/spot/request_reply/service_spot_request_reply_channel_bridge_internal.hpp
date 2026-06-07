/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_SERVICE_SPOT_REQUEST_REPLY_CHANNEL_BRIDGE_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_SERVICE_SPOT_REQUEST_REPLY_CHANNEL_BRIDGE_INTERNAL_HPP_INCLUDED__

#include "zlink.h"

#include <stddef.h>
#include <stdint.h>

namespace zlink
{
class socket_base_t;

namespace spot_reqrep_internal
{
zlink_submit_result_t start_spot_channel_request_bridge (void *spot_,
                                                         socket_base_t *router_,
                                                         zlink_msg_t *parts_,
                                                         size_t part_count_,
                                                         zlink_reply_handler_fn handler_,
                                                         void *userdata_,
                                                         zlink_send_flags_t flags_,
                                                         uint32_t timeout_ms_);
}
}

#endif
