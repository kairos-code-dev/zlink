/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SERVICE_SPOT_ACTOR_MULTIPART_INTERNAL_HPP_INCLUDED
#define ZLINK_SERVICE_SPOT_ACTOR_MULTIPART_INTERNAL_HPP_INCLUDED

#include "services/spot/common/spot_message_parts_internal.hpp"

#include <stddef.h>

namespace zlink
{
namespace spot_actor_internal
{

bool valid_multipart_payload (zlink_msg_t *parts_, size_t part_count_);
void consume_multipart_payload (zlink_msg_t *parts_, size_t part_count_);
zlink_submit_result_t adopt_multipart_payload (
  spot_owned_msg_parts_t *out_, zlink_msg_t *parts_, size_t part_count_);

}
}

#endif
