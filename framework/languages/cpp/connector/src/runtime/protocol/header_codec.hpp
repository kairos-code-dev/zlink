/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/stream_connector/contracts/result.hpp>
#include <zlink/stream_connector/contracts/zlink_stream_models.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace zlink::stream_connector::detail
{

struct stream_header_t
{
    message_kind_t kind = message_kind_t::send;
    codec_t codec = codec_t::raw;
    header_flags_t flags = header_flags_t::none;
    std::optional<std::uint64_t> request_seq;
    std::string name;
    metadata_t metadata;
};

class header_codec_t
{
  public:
    result_t<std::vector<std::uint8_t>> encode (const stream_header_t &header) const;
    result_t<stream_header_t> decode (const std::vector<std::uint8_t> &bytes) const;
};

} // namespace zlink::stream_connector::detail
