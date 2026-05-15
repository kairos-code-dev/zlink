/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/actor/multipart/service_spot_actor_multipart_internal.hpp"

#include "api/message/submit_result_internal.hpp"

#include <errno.h>

namespace zlink
{
namespace spot_actor_internal
{

bool valid_multipart_payload (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_ && part_count_ == 0)
        return true;
    if (!parts_ || part_count_ == 0) {
        errno = EINVAL;
        return false;
    }
    for (size_t i = 0; i < part_count_; ++i) {
        if (!spot_msg_frame_valid (parts_[i])) {
            errno = EFAULT;
            return false;
        }
    }
    return true;
}

void consume_multipart_payload (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < part_count_; ++i) {
        (void) zlink_msg_close (&parts_[i]);
        (void) zlink_msg_init (&parts_[i]);
    }
}

zlink_submit_result_t adopt_multipart_payload (
  spot_owned_msg_parts_t *out_, zlink_msg_t *parts_, size_t part_count_)
{
    if (!out_ || !valid_multipart_payload (parts_, part_count_))
        return ZLINK_SUBMIT_INVALID_ARGUMENT;

    spot_clear_msg_parts (out_);
    for (size_t i = 0; i < part_count_; ++i) {
        out_->push_back (zlink_msg_t ());
        spot_init_msg_frame (&out_->back ());
        if (zlink_msg_adopt (&out_->back (), &parts_[i]) != ZLINK_CONFIG_OK) {
            const int err = errno;
            spot_clear_msg_parts (out_);
            errno = err;
            return submit_result_internal::from_errno (err);
        }
    }
    return ZLINK_SUBMIT_OK;
}

}
}
