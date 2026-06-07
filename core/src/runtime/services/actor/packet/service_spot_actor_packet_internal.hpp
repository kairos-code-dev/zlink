/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SERVICE_SPOT_ACTOR_PACKET_INTERNAL_HPP_INCLUDED
#define ZLINK_SERVICE_SPOT_ACTOR_PACKET_INTERNAL_HPP_INCLUDED

#include <zlink.h>

namespace zlink
{
namespace spot_actor_internal
{

zlink_submit_result_t
build_packet_frame (zlink_msg_t *header_, zlink_msg_t *body_, zlink_msg_t *frame_out_);
zlink_submit_result_t copy_msg_for_stream_send (zlink_msg_t *src_, zlink_msg_t *dst_);
zlink_submit_result_t send_copied_msg_to_bound_stream (void *stream_,
                                                       const zlink_routing_id_t *rid_,
                                                       zlink_msg_t *message_,
                                                       zlink_send_flags_t flags_);

}
}

#endif
