/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/protocol/header_codec.hpp"

#include "runtime/protocol/metadata_codec.hpp"

#include <limits>

namespace zlink::stream_connector::detail
{

namespace
{

constexpr std::uint8_t known_flags = static_cast<std::uint8_t> (header_flags_t::has_request_seq)
                                     | static_cast<std::uint8_t> (header_flags_t::has_metadata)
                                     | static_cast<std::uint8_t> (header_flags_t::payload_compressed);

bool has_flag (header_flags_t flags, header_flags_t flag)
{
    return (static_cast<std::uint8_t> (flags) & static_cast<std::uint8_t> (flag)) != 0;
}

void set_flag (header_flags_t &flags, header_flags_t flag)
{
    flags = flags | flag;
}

void clear_flag (header_flags_t &flags, header_flags_t flag)
{
    flags = static_cast<header_flags_t> (static_cast<std::uint8_t> (flags) & ~static_cast<std::uint8_t> (flag));
}

void write_u16 (std::vector<std::uint8_t> &bytes, std::uint16_t value)
{
    bytes.push_back (static_cast<std::uint8_t> ((value >> 8) & 0xff));
    bytes.push_back (static_cast<std::uint8_t> (value & 0xff));
}

void write_u64 (std::vector<std::uint8_t> &bytes, std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back (static_cast<std::uint8_t> ((value >> shift) & 0xff));
    }
}

std::uint16_t read_u16 (const std::vector<std::uint8_t> &bytes, std::size_t &offset)
{
    const auto value = static_cast<std::uint16_t> ((bytes[offset] << 8) | bytes[offset + 1]);
    offset += 2;
    return value;
}

std::uint64_t read_u64 (const std::vector<std::uint8_t> &bytes, std::size_t &offset)
{
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | bytes[offset + static_cast<std::size_t> (i)];
    }
    offset += 8;
    return value;
}

bool is_defined (message_kind_t kind)
{
    switch (kind) {
        case message_kind_t::send:
        case message_kind_t::request:
        case message_kind_t::response:
        case message_kind_t::error:
        case message_kind_t::control:
            return true;
    }
    return false;
}

bool is_defined (codec_t codec)
{
    switch (codec) {
        case codec_t::raw:
        case codec_t::json:
        case codec_t::message_pack:
        case codec_t::protobuf:
            return true;
    }
    return false;
}

result_t<void> validate_header (const stream_header_t &header)
{
    if (!is_defined (header.kind) || !is_defined (header.codec)
        || (static_cast<std::uint8_t> (header.flags) & ~known_flags) != 0) {
        return result_t<void>::failure (error_code_t::frame_decode_failed, "Unknown stream header enum value.");
    }
    const auto has_request_seq = has_flag (header.flags, header_flags_t::has_request_seq);
    const auto has_metadata = has_flag (header.flags, header_flags_t::has_metadata);
    if (header.kind == message_kind_t::send && has_request_seq) {
        return result_t<void>::failure (error_code_t::frame_decode_failed,
                                        "Send packet must not contain a request sequence.");
    }
    if ((header.kind == message_kind_t::request || header.kind == message_kind_t::response) && !has_request_seq) {
        return result_t<void>::failure (error_code_t::frame_decode_failed,
                                        "Request and response packets must contain a request sequence.");
    }
    if (header.kind == message_kind_t::error && header.codec != codec_t::json) {
        return result_t<void>::failure (error_code_t::frame_decode_failed, "Error packet must use the JSON codec.");
    }
    if (header.kind == message_kind_t::control) {
        if (header.flags != header_flags_t::none || header.codec != codec_t::raw || has_request_seq || has_metadata) {
            return result_t<void>::failure (error_code_t::frame_decode_failed,
                                            "Control packet must use raw codec and no flags.");
        }
    }
    if (header.name.empty () || header.name.size () > std::numeric_limits<std::uint8_t>::max ()) {
        return result_t<void>::failure (error_code_t::validation_failed, "Packet name length is invalid.");
    }
    if (!header.name.empty () && header.name.front () == '$' && header.kind != message_kind_t::control) {
        return result_t<void>::failure (error_code_t::frame_decode_failed,
                                        "Reserved packet names are only valid for control packets.");
    }
    if (header.request_seq && *header.request_seq == 0) {
        return result_t<void>::failure (error_code_t::validation_failed, "Request sequence must not be zero.");
    }
    return result_t<void>::success ();
}

} // namespace

result_t<std::vector<std::uint8_t>> header_codec_t::encode (const stream_header_t &source) const
{
    auto header = source;
    if (header.request_seq) {
        set_flag (header.flags, header_flags_t::has_request_seq);
    } else {
        clear_flag (header.flags, header_flags_t::has_request_seq);
    }
    if (!header.metadata.values.empty ()) {
        set_flag (header.flags, header_flags_t::has_metadata);
    } else {
        clear_flag (header.flags, header_flags_t::has_metadata);
    }
    if (auto validation = validate_header (header); !validation) {
        return result_t<std::vector<std::uint8_t>>::failure (validation.error_code (), validation.error ()->message);
    }
    auto metadata = metadata_codec_t::encode (header.metadata);
    if (!metadata) {
        return result_t<std::vector<std::uint8_t>>::failure (metadata.error_code (), metadata.error ()->message);
    }
    if (metadata.value ().size () > std::numeric_limits<std::uint16_t>::max ()) {
        return result_t<std::vector<std::uint8_t>>::failure (error_code_t::validation_failed,
                                                             "Metadata payload exceeds fixed limit.");
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve (3 + (header.request_seq ? 8 : 0) + 1 + header.name.size ()
                   + (metadata.value ().empty () ? 0 : 2 + metadata.value ().size ()));
    bytes.push_back (static_cast<std::uint8_t> (header.kind));
    bytes.push_back (static_cast<std::uint8_t> (header.codec));
    bytes.push_back (static_cast<std::uint8_t> (header.flags));
    if (header.request_seq) {
        write_u64 (bytes, *header.request_seq);
    }
    bytes.push_back (static_cast<std::uint8_t> (header.name.size ()));
    bytes.insert (bytes.end (), header.name.begin (), header.name.end ());
    if (!metadata.value ().empty ()) {
        write_u16 (bytes, static_cast<std::uint16_t> (metadata.value ().size ()));
        bytes.insert (bytes.end (), metadata.value ().begin (), metadata.value ().end ());
    }
    return result_t<std::vector<std::uint8_t>>::success (std::move (bytes));
}

result_t<stream_header_t> header_codec_t::decode (const std::vector<std::uint8_t> &bytes) const
{
    if (bytes.size () < 4) {
        return result_t<stream_header_t>::failure (error_code_t::frame_decode_failed, "Helper header is too short.");
    }
    std::size_t offset = 0;
    stream_header_t header;
    header.kind = static_cast<message_kind_t> (bytes[offset++]);
    header.codec = static_cast<codec_t> (bytes[offset++]);
    header.flags = static_cast<header_flags_t> (bytes[offset++]);

    if (has_flag (header.flags, header_flags_t::has_request_seq)) {
        if (bytes.size () - offset < 8) {
            return result_t<stream_header_t>::failure (error_code_t::frame_decode_failed,
                                                       "Helper header request sequence is incomplete.");
        }
        header.request_seq = read_u64 (bytes, offset);
    }
    if (bytes.size () - offset < 1) {
        return result_t<stream_header_t>::failure (error_code_t::frame_decode_failed,
                                                   "Helper header name length is missing.");
    }
    const auto name_size = bytes[offset++];
    if (name_size == 0 || bytes.size () - offset < name_size) {
        return result_t<stream_header_t>::failure (error_code_t::frame_decode_failed,
                                                   "Helper header packet name is invalid.");
    }
    header.name = std::string (bytes.begin () + static_cast<std::ptrdiff_t> (offset),
                               bytes.begin () + static_cast<std::ptrdiff_t> (offset + name_size));
    offset += name_size;

    if (has_flag (header.flags, header_flags_t::has_metadata)) {
        if (bytes.size () - offset < 2) {
            return result_t<stream_header_t>::failure (error_code_t::frame_decode_failed,
                                                       "Helper header metadata length is missing.");
        }
        const auto metadata_size = read_u16 (bytes, offset);
        if (bytes.size () - offset < metadata_size) {
            return result_t<stream_header_t>::failure (error_code_t::frame_decode_failed,
                                                       "Helper header metadata is incomplete.");
        }
        std::vector<std::uint8_t> metadata (bytes.begin () + static_cast<std::ptrdiff_t> (offset),
                                            bytes.begin () + static_cast<std::ptrdiff_t> (offset + metadata_size));
        offset += metadata_size;
        auto decoded = metadata_codec_t::decode (metadata);
        if (!decoded) {
            return result_t<stream_header_t>::failure (decoded.error_code (), decoded.error ()->message);
        }
        header.metadata = decoded.value ();
    }
    if (offset != bytes.size ()) {
        return result_t<stream_header_t>::failure (error_code_t::frame_decode_failed,
                                                   "Helper header contains trailing bytes.");
    }
    if (auto validation = validate_header (header); !validation) {
        return result_t<stream_header_t>::failure (validation.error_code (), validation.error ()->message);
    }
    return result_t<stream_header_t>::success (std::move (header));
}

} // namespace zlink::stream_connector::detail
