/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/actor/gateway/service_spot_actor_gateway_protocol_internal.hpp"

#include "protocol/wire.hpp"
#include "services/actor/validation/service_spot_actor_validation_internal.hpp"

#include <string.h>

namespace zlink
{
namespace spot_actor_gateway
{

namespace
{
const unsigned char actor_gateway_magic[4] = {'Z', 'A', 'G', '1'};
const size_t control_header_size = 30u;
}

const char endpoint_name[] = "__zlink.actor-gateway";

frame_t::frame_t () :
    kind (0),
    part_flag (ZLINK_PART_FINAL),
    generation (0),
    request_id (0),
    join_result_code (0)
{
    memset (&session_rid, 0, sizeof (session_rid));
    memset (actor_id, 0, sizeof (actor_id));
}

bool init_control_msg (uint8_t kind_,
                       const zlink_routing_id_t &session_rid_,
                       const char *actor_id_,
                       uint64_t generation_,
                       zlink_part_flag_t part_flag_,
                       zlink_msg_t *out_,
                       uint64_t request_id_,
                       int32_t join_result_code_)
{
    if (!out_ || !spot_actor_internal::valid_routing_id (&session_rid_)
        || !spot_actor_internal::valid_actor_id (actor_id_)) {
        errno = EINVAL;
        return false;
    }

    const size_t actor_id_len = strlen (actor_id_);
    if (actor_id_len > UINT16_MAX || session_rid_.size > sizeof (session_rid_.data)) {
        errno = EINVAL;
        return false;
    }

    const size_t frame_size = control_header_size + session_rid_.size + actor_id_len;
    memset (out_, 0, sizeof (*out_));
    if (zlink_msg_init_size (out_, frame_size) != ZLINK_CONFIG_OK)
        return false;

    unsigned char *data = static_cast<unsigned char *> (zlink_msg_data (out_));
    memcpy (data, actor_gateway_magic, sizeof (actor_gateway_magic));
    data[4] = kind_;
    data[5] = static_cast<unsigned char> (part_flag_);
    data[6] = session_rid_.size;
    data[7] = 0;
    zlink::put_uint64 (data + 8, generation_);
    zlink::put_uint16 (data + 16, static_cast<uint16_t> (actor_id_len));
    zlink::put_uint64 (data + 18, request_id_);
    zlink::put_uint32 (data + 26, static_cast<uint32_t> (join_result_code_));
    if (session_rid_.size > 0)
        memcpy (data + control_header_size, session_rid_.data, session_rid_.size);
    if (actor_id_len > 0)
        memcpy (data + control_header_size + session_rid_.size, actor_id_, actor_id_len);
    return true;
}

bool parse_control_msg (zlink_msg_t *msg_, frame_t *out_)
{
    if (!msg_ || !out_) {
        errno = EINVAL;
        return false;
    }

    const size_t size = zlink_msg_size (msg_);
    const unsigned char *data = static_cast<const unsigned char *> (zlink_msg_data (msg_));
    if (size < control_header_size
        || memcmp (data, actor_gateway_magic, sizeof (actor_gateway_magic)) != 0) {
        errno = EPROTO;
        return false;
    }

    const uint8_t session_size = data[6];
    const uint16_t actor_id_len = zlink::get_uint16 (data + 16);
    if (session_size == 0 || actor_id_len == 0 || actor_id_len >= ZLINK_ACTOR_ID_MAX
        || size != control_header_size + session_size + actor_id_len) {
        errno = EPROTO;
        return false;
    }

    frame_t frame;
    frame.kind = data[4];
    frame.part_flag = static_cast<zlink_part_flag_t> (data[5]);
    frame.session_rid.size = session_size;
    memcpy (frame.session_rid.data, data + control_header_size, session_size);
    frame.generation = zlink::get_uint64 (data + 8);
    frame.request_id = zlink::get_uint64 (data + 18);
    frame.join_result_code = static_cast<int32_t> (zlink::get_uint32 (data + 26));
    memcpy (frame.actor_id, data + control_header_size + session_size, actor_id_len);
    frame.actor_id[actor_id_len] = '\0';
    if (!spot_actor_internal::valid_routing_id (&frame.session_rid)
        || !spot_actor_internal::valid_actor_id (frame.actor_id)) {
        errno = EPROTO;
        return false;
    }

    *out_ = frame;
    return true;
}

}
}
