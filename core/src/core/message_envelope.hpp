/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_CORE_MESSAGE_ENVELOPE_HPP_INCLUDED__
#define __ZLINK_CORE_MESSAGE_ENVELOPE_HPP_INCLUDED__

#include <stddef.h>

#include <zlink.h>

namespace zlink
{
class msg_t;
class socket_base_t;

bool message_has_request_reply_envelope (const msg_t &msg_);
bool message_has_user_metadata_envelope (const msg_t &msg_);

int encode_request_reply_envelope (const msg_t &src_, msg_t *dst_);
int encode_user_metadata_envelope (const msg_t &src_, msg_t *dst_);

int strip_internal_message_envelopes (socket_base_t *socket_, zlink_msg_t *msg_);
}

#endif
