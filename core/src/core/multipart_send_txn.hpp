/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_CORE_MULTIPART_SEND_TXN_HPP_INCLUDED__
#define __ZLINK_CORE_MULTIPART_SEND_TXN_HPP_INCLUDED__

#include <zlink.h>

namespace zlink
{
class socket_base_t;

int logical_multipart_send (socket_base_t *socket_,
                            zlink_msg_t *parts_,
                            size_t part_count_,
                            int flags_);

int logical_multipart_send_routed (socket_base_t *socket_,
                                   const zlink_routing_id_t *routing_id_,
                                   zlink_msg_t *parts_,
                                   size_t part_count_,
                                   int flags_);

int logical_multipart_send_prefixed (socket_base_t *socket_,
                                     const void *prefix_data_,
                                     size_t prefix_size_,
                                     zlink_msg_t *parts_,
                                     size_t part_count_,
                                     int flags_,
                                     int route_ready_retry_ms_ = 0);

int logical_multipart_publish (socket_base_t *socket_,
                               const char *topic_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               int flags_,
                               bool fallback_on_missing_sndtimeo_ = false);
}

#endif
