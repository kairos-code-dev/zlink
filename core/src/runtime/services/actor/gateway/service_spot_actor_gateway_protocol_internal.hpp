/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SERVICE_SPOT_ACTOR_GATEWAY_PROTOCOL_INTERNAL_HPP_INCLUDED
#define ZLINK_SERVICE_SPOT_ACTOR_GATEWAY_PROTOCOL_INTERNAL_HPP_INCLUDED

#include "zlink.h"

#include <stdint.h>

namespace zlink
{
namespace spot_actor_gateway
{

enum packet_kind_t
{
    packet_session_to_actor = 1,
    packet_actor_to_session = 2,
    packet_entry_join_request = 3,
    packet_entry_join_reply = 4,
    packet_spot_join_request = 5,
    packet_spot_join_reply = 6
};

extern const char endpoint_name[];

struct frame_t
{
    frame_t ();

    uint8_t kind;
    zlink_part_flag_t part_flag;
    zlink_routing_id_t session_rid;
    char actor_id[ZLINK_ACTOR_ID_MAX];
    uint64_t generation;
    uint64_t request_id;
    int32_t join_result_code;
};

bool init_control_msg (uint8_t kind_,
                       const zlink_routing_id_t &session_rid_,
                       const char *actor_id_,
                       uint64_t generation_,
                       zlink_part_flag_t part_flag_,
                       zlink_msg_t *out_,
                       uint64_t request_id_ = 0,
                       int32_t join_result_code_ = 0);

bool parse_control_msg (zlink_msg_t *msg_, frame_t *out_);

}
}

#endif
