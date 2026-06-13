/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/protocol/framing.hpp"

#include "runtime/protocol/compression/lz4_compression_codec.hpp"
#include "runtime/protocol/framing/frame_codec.hpp"
#include "runtime/protocol/header_codec.hpp"
#include "runtime/transport/stream_connection.hpp"

#include <chrono>
#include <optional>
#include <stdexcept>

namespace zlink::stream_connector::detail
{

namespace
{

zlink::message_t message_from_bytes (const std::vector<std::uint8_t> &bytes)
{
    return zlink::message_t::from (bytes);
}

bool has_flag (header_flags_t flags, header_flags_t flag) noexcept
{
    return (static_cast<std::uint8_t> (flags) & static_cast<std::uint8_t> (flag)) != 0;
}

std::optional<packet_t> read_stream_packet (connector_state_t &state)
{
    auto prefix = read_exact (state, 6);
    const auto header_size = static_cast<std::size_t> ((prefix[0] << 8) | prefix[1]);
    const auto payload_size =
      (static_cast<std::size_t> (prefix[2]) << 24) | (static_cast<std::size_t> (prefix[3]) << 16)
      | (static_cast<std::size_t> (prefix[4]) << 8) | static_cast<std::size_t> (prefix[5]);
    if (!frame_codec_t::validate_receive_frame_size (header_size, payload_size, state.options)) {
        state.inbound_error =
          error_t{error_code_t::frame_too_large, "Inbound stream frame exceeds configured limits."};
        boost::system::error_code ignored;
        if (state.connection) {
            state.connection->close (ignored);
        }
        return std::nullopt;
    }
    auto header_bytes = read_exact (state, header_size);
    auto payload_bytes = read_exact (state, payload_size);
    auto decoded = header_codec_t{}.decode (header_bytes);
    if (!decoded) {
        return packet_t{"unknown", {}, codec_t::raw, false, zlink::message_t::from (std::string{})};
    }
    auto header = decoded.value ();
    state.last_inbound_received = std::chrono::steady_clock::now ();
    const bool compressed = has_flag (header.flags, header_flags_t::payload_compressed);
    auto payload = message_from_bytes (payload_bytes);
    if (compressed) {
        if (state.options.compression != compression_t::lz4) {
            throw std::runtime_error ("stream connector compression is not configured");
        }
        if (!state.lz4_enabled) {
            throw std::runtime_error ("LZ4 compression is not enabled");
        }
        payload = lz4_compression_codec_t{}.decompress (payload);
    }
    packet_t packet;
    packet.name = std::move (header.name);
    packet.metadata = std::move (header.metadata);
    packet.codec = header.codec;
    packet.compressed = compressed;
    packet.payload = std::move (payload);
    return packet;
}

} // namespace

void dispatch_packet (connector_state_t &state, const packet_t &packet)
{
    const auto found = state.packet_handlers.find (packet.name);
    if (found == state.packet_handlers.end ()) {
        return;
    }
    for (const auto &handler : found->second) {
        handler (packet);
    }
}

std::vector<packet_t> drain_available_pushes (connector_state_t &state)
{
    std::vector<packet_t> packets;
    while (state.connection && state.connection->is_open ()) {
        boost::system::error_code error;
        if (state.connection->available (error) == 0 || error) {
            return packets;
        }
        auto packet = read_stream_packet (state);
        if (!packet) {
            return packets;
        }
        packets.push_back (std::move (*packet));
    }
    return packets;
}

} // namespace zlink::stream_connector::detail
