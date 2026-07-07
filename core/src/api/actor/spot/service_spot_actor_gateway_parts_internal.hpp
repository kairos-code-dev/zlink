/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_ACTOR_SPOT_SERVICE_SPOT_ACTOR_GATEWAY_PARTS_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_ACTOR_SPOT_SERVICE_SPOT_ACTOR_GATEWAY_PARTS_INTERNAL_HPP_INCLUDED__

#include <zlink.h>

#include <errno.h>
#include <stddef.h>
#include <vector>

#include "api/socket/request_reply_protocol_internal.hpp"

namespace zlink
{
namespace spot_actor_internal
{
inline int build_gateway_parts (zlink_msg_t *control_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                bool consume_payload_on_error_,
                                std::vector<zlink_msg_t> *out_)
{
    if (!control_ || !out_) {
        errno = EINVAL;
        return -1;
    }

    out_->resize (part_count_ + 1);
    for (size_t i = 0; i < out_->size (); ++i)
        zlink_msg_init (&(*out_)[i]);

    if (zlink_msg_move (&(*out_)[0], control_) != 0) {
        const int saved_errno = errno;
        (void) zlink_msg_close (control_);
        zlink::request_reply::close_built_parts (out_);
        errno = saved_errno;
        return -1;
    }

    for (size_t i = 0; i < part_count_; ++i) {
        if (zlink_msg_move (&(*out_)[i + 1], &parts_[i]) != 0) {
            const int saved_errno = errno;
            zlink::request_reply::close_built_parts (out_);
            if (consume_payload_on_error_)
                zlink::request_reply::consume_send_frames_from (parts_, i, part_count_);
            errno = saved_errno;
            return -1;
        }
    }
    return 0;
}
}
}

#endif
