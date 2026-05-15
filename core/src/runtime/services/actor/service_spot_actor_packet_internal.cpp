/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/actor/service_spot_actor_packet_internal.hpp"

#include "protocol/wire.hpp"

#include <errno.h>
#include <stdint.h>
#include <string.h>

namespace zlink
{
namespace spot_actor_internal
{

zlink_submit_result_t build_packet_frame (zlink_msg_t *header_,
                                          zlink_msg_t *body_,
                                          zlink_msg_t *frame_out_)
{
    const size_t header_size = zlink_msg_size (header_);
    const size_t body_size = zlink_msg_size (body_);
    if (header_size > UINT16_MAX || body_size > UINT32_MAX
        || header_size > SIZE_MAX - body_size - 6u) {
        errno = EMSGSIZE;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    const size_t total_size = 6u + header_size + body_size;
    memset (frame_out_, 0, sizeof (*frame_out_));
    if (zlink_msg_init_size (frame_out_, total_size) != ZLINK_CONFIG_OK)
        return ZLINK_SUBMIT_INTERNAL_ERROR;

    unsigned char *data =
      static_cast<unsigned char *> (zlink_msg_data (frame_out_));
    zlink::put_uint16 (data, static_cast<uint16_t> (header_size));
    zlink::put_uint32 (data + 2, static_cast<uint32_t> (body_size));
    if (header_size > 0)
        memcpy (data + 6, zlink_msg_data (header_), header_size);
    if (body_size > 0)
        memcpy (data + 6 + header_size, zlink_msg_data (body_), body_size);
    return ZLINK_SUBMIT_OK;
}

}
}
