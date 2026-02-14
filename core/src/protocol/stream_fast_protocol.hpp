/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_STREAM_FAST_PROTOCOL_HPP_INCLUDED__
#define __ZLINK_STREAM_FAST_PROTOCOL_HPP_INCLUDED__

#include <stddef.h>

namespace zlink
{
namespace stream_fast_protocol
{
// STREAM fast wire protocol:
//   [4-byte big-endian payload length][payload bytes]
const size_t length_prefix_size = 4;
}
}

#endif
