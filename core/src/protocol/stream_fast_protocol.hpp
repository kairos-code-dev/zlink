/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_STREAM_FAST_PROTOCOL_HPP_INCLUDED__
#define __ZLINK_STREAM_FAST_PROTOCOL_HPP_INCLUDED__

#include <stddef.h>

namespace zlink
{
namespace stream_fast_protocol
{
const unsigned char version = 0x01;
const unsigned char type_data = 0x00;
const unsigned char type_connect = 0x01;
const unsigned char type_disconnect = 0x02;
const unsigned char magic0 = 0x5A; // 'Z'
const unsigned char magic1 = 0x4C; // 'L'
const size_t header_size = 8;
}
}

#endif
