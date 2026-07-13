/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/protocol/compression/lz4_compression_codec.hpp"

#ifdef ZLINK_STREAM_CONNECTOR_WITH_LZ4
#include <lz4.h>
#endif

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#include <memory>

namespace zlink::stream_connector::detail
{

namespace
{

void write_u32 (std::string &output, std::uint32_t value)
{
    output.push_back (static_cast<char> ((value >> 24) & 0xff));
    output.push_back (static_cast<char> ((value >> 16) & 0xff));
    output.push_back (static_cast<char> ((value >> 8) & 0xff));
    output.push_back (static_cast<char> (value & 0xff));
}

std::uint32_t read_u32 (std::span<const std::byte> input)
{
    return (std::to_integer<std::uint32_t> (input[0]) << 24)
           | (std::to_integer<std::uint32_t> (input[1]) << 16)
           | (std::to_integer<std::uint32_t> (input[2]) << 8)
           | std::to_integer<std::uint32_t> (input[3]);
}

} // namespace

bool lz4_compression_codec_t::available () noexcept
{
#ifdef ZLINK_STREAM_CONNECTOR_WITH_LZ4
    return true;
#else
    return false;
#endif
}

zlink::message_t lz4_compression_codec_t::compress (const zlink::message_t &payload) const
{
#ifndef ZLINK_STREAM_CONNECTOR_WITH_LZ4
    throw std::runtime_error ("LZ4 compression is not linked into this connector build");
#else
    const auto input = payload.bytes ();
    if (input.size () > static_cast<std::size_t> (std::numeric_limits<std::int32_t>::max ())) {
        throw std::runtime_error ("LZ4 input exceeds fixed size limit");
    }
    std::string output;
    write_u32 (output, static_cast<std::uint32_t> (input.size ()));
    if (input.empty ()) {
        return zlink::message_t::from (output);
    }
    const int bound = LZ4_compressBound (static_cast<int> (input.size ()));
    std::vector<char> compressed (static_cast<std::size_t> (bound));
    const auto *input_data = reinterpret_cast<const char *> (input.data ());
    const int written = LZ4_compress_default (input_data, compressed.data (),
                                              static_cast<int> (input.size ()), bound);
    if (written <= 0) {
        throw std::runtime_error ("LZ4 compression failed");
    }
    output.append (compressed.data (), static_cast<std::size_t> (written));
    return zlink::message_t::from (output);
#endif
}

zlink::message_t
lz4_compression_codec_t::decompress (const zlink::message_t &payload,
                                     std::size_t max_decompressed_size) const
{
#ifndef ZLINK_STREAM_CONNECTOR_WITH_LZ4
    throw std::runtime_error ("LZ4 decompression is not linked into this connector build");
#else
    const auto input = payload.bytes ();
    if (input.size () < 4) {
        throw std::runtime_error ("LZ4 payload is missing original size");
    }
    const auto original_size = read_u32 (input);
    if (original_size > static_cast<std::uint32_t> (std::numeric_limits<std::int32_t>::max ())) {
        throw std::runtime_error ("LZ4 payload exceeds fixed size limit");
    }
    if (original_size > max_decompressed_size) {
        throw std::runtime_error ("LZ4 payload exceeds configured receive limit");
    }
    if (original_size == 0) {
        return zlink::message_t::from (std::string{});
    }
    std::string output (original_size, '\0');
    const auto *input_data = reinterpret_cast<const char *> (input.data ());
    const int decoded =
      LZ4_decompress_safe (input_data + 4, output.data (), static_cast<int> (input.size () - 4),
                           static_cast<int> (output.size ()));
    if (decoded != static_cast<int> (output.size ())) {
        throw std::runtime_error ("LZ4 decompression failed");
    }
    return zlink::message_t::from (output);
#endif
}

} // namespace zlink::stream_connector::detail

namespace zlink::stream_connector
{

std::shared_ptr<const compression_codec_t> lz4_compression_codec ()
{
    static const auto codec = std::make_shared<const detail::lz4_compression_codec_t> ();
    return codec;
}

} // namespace zlink::stream_connector
