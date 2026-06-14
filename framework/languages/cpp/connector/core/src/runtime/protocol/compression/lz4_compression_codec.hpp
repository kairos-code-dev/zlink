/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>

#include <cstddef>

namespace zlink::stream_connector::detail
{

class lz4_compression_codec_t
{
  public:
    static bool available () noexcept;
    zlink::message_t compress (const zlink::message_t &payload) const;
    zlink::message_t decompress (const zlink::message_t &payload,
                                 std::size_t max_decompressed_size) const;
};

} // namespace zlink::stream_connector::detail
