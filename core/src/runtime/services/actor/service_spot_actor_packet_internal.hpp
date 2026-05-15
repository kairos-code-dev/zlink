/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SERVICE_SPOT_ACTOR_PACKET_INTERNAL_HPP_INCLUDED
#define ZLINK_SERVICE_SPOT_ACTOR_PACKET_INTERNAL_HPP_INCLUDED

#include <zlink.h>

namespace zlink
{
namespace spot_actor_internal
{

zlink_submit_result_t build_packet_frame (zlink_msg_t *header_,
                                          zlink_msg_t *body_,
                                          zlink_msg_t *frame_out_);

}
}

#endif
