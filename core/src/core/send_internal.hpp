/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_CORE_SEND_INTERNAL_HPP_INCLUDED__
#define __ZLINK_CORE_SEND_INTERNAL_HPP_INCLUDED__

#include <zlink.h>

namespace zlink
{
int send_msg_internal (void *socket_, zlink_msg_t *msg_, int flags_);
}

#endif
