/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_CORE_RECV_INTERNAL_HPP_INCLUDED__
#define __ZLINK_CORE_RECV_INTERNAL_HPP_INCLUDED__

#include <zlink.h>

namespace zlink
{
class socket_base_t;

int recv_msg_socket (socket_base_t *socket_,
                     int socket_type_,
                     zlink_msg_t *msg_,
                     int flags_);
int recv_msg_internal (void *socket_, zlink_msg_t *msg_, int flags_);
int recv_msg_routed_socket (socket_base_t *socket_,
                            zlink_msg_t *msg_,
                            zlink_routing_id_t *source_rid_out_,
                            int flags_);
int recv_msg_routed_internal (void *socket_,
                              zlink_msg_t *msg_,
                              zlink_routing_id_t *source_rid_out_,
                              int flags_);
int recv_followup_msg_socket (socket_base_t *socket_, zlink_msg_t *msg_);
int recv_followup_msg_internal (void *socket_, zlink_msg_t *msg_);
int recv_buffer_internal (void *socket_,
                          void *buf_,
                          size_t len_,
                          int flags_);
int wait_socket_events_internal (void *socket_, short events_, long timeout_ms_);
}

#endif
