/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>
#include <zlink/Contracts/Messaging/received.hpp>
#include <zlink/Contracts/Service/operation_contracts.hpp>
#include <zlink/Contracts/Sockets/results.hpp>
#include <zlink/codec/json.hpp>
#include <zlink/stream_connector.hpp>

#include <chrono>
#include <cstdint>
#include <cerrno>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace zlink::samples
{

struct stream_server_frame_t
{
    zlink::stream_connector::message_kind_t kind = zlink::stream_connector::message_kind_t::send;
    std::optional<std::uint64_t> request_seq;
    std::string name;
    std::string payload;
};

inline bool has_request_seq (zlink::stream_connector::header_flags_t flags)
{
    return (static_cast<std::uint8_t> (flags)
            & static_cast<std::uint8_t> (zlink::stream_connector::header_flags_t::has_request_seq))
           != 0;
}

inline void write_u16 (std::vector<std::uint8_t> &bytes, std::uint16_t value)
{
    bytes.push_back (static_cast<std::uint8_t> ((value >> 8) & 0xff));
    bytes.push_back (static_cast<std::uint8_t> (value & 0xff));
}

inline void write_u32 (std::vector<std::uint8_t> &bytes, std::uint32_t value)
{
    bytes.push_back (static_cast<std::uint8_t> ((value >> 24) & 0xff));
    bytes.push_back (static_cast<std::uint8_t> ((value >> 16) & 0xff));
    bytes.push_back (static_cast<std::uint8_t> ((value >> 8) & 0xff));
    bytes.push_back (static_cast<std::uint8_t> (value & 0xff));
}

inline void write_u64 (std::vector<std::uint8_t> &bytes, std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back (static_cast<std::uint8_t> ((value >> shift) & 0xff));
    }
}

inline std::uint64_t read_u64 (const std::string &bytes, std::size_t &offset)
{
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | static_cast<std::uint8_t> (bytes[offset++]);
    }
    return value;
}

inline std::optional<stream_server_frame_t> try_read_stream_frame (std::string &buffer)
{
    if (buffer.size () < 6) {
        return std::nullopt;
    }
    const auto header_size =
      (static_cast<std::size_t> (static_cast<std::uint8_t> (buffer[0])) << 8) | static_cast<std::uint8_t> (buffer[1]);
    const auto payload_size = (static_cast<std::size_t> (static_cast<std::uint8_t> (buffer[2])) << 24)
                              | (static_cast<std::size_t> (static_cast<std::uint8_t> (buffer[3])) << 16)
                              | (static_cast<std::size_t> (static_cast<std::uint8_t> (buffer[4])) << 8)
                              | static_cast<std::uint8_t> (buffer[5]);
    if (buffer.size () < 6 + header_size + payload_size) {
        return std::nullopt;
    }

    std::size_t offset = 6;
    stream_server_frame_t frame;
    frame.kind = static_cast<zlink::stream_connector::message_kind_t> (static_cast<std::uint8_t> (buffer[offset++]));
    (void) buffer[offset++];
    const auto flags =
      static_cast<zlink::stream_connector::header_flags_t> (static_cast<std::uint8_t> (buffer[offset++]));
    if (has_request_seq (flags)) {
        frame.request_seq = read_u64 (buffer, offset);
    }
    const auto name_size = static_cast<std::uint8_t> (buffer[offset++]);
    frame.name = buffer.substr (offset, name_size);
    offset += name_size;
    frame.payload = buffer.substr (6 + header_size, payload_size);
    buffer.erase (0, 6 + header_size + payload_size);
    return frame;
}

inline zlink::message_t make_stream_frame (zlink::stream_connector::message_kind_t kind,
                                           std::optional<std::uint64_t> request_seq,
                                           std::string name,
                                           std::string payload)
{
    std::vector<std::uint8_t> header;
    header.push_back (static_cast<std::uint8_t> (kind));
    header.push_back (static_cast<std::uint8_t> (zlink::stream_connector::codec_t::raw));
    header.push_back (request_seq ? static_cast<std::uint8_t> (zlink::stream_connector::header_flags_t::has_request_seq)
                                  : 0);
    if (request_seq) {
        write_u64 (header, *request_seq);
    }
    header.push_back (static_cast<std::uint8_t> (name.size ()));
    header.insert (header.end (), name.begin (), name.end ());

    std::vector<std::uint8_t> frame;
    write_u16 (frame, static_cast<std::uint16_t> (header.size ()));
    write_u32 (frame, static_cast<std::uint32_t> (payload.size ()));
    frame.insert (frame.end (), header.begin (), header.end ());
    frame.insert (frame.end (), payload.begin (), payload.end ());
    return zlink::message_t::from (std::string (frame.begin (), frame.end ()));
}

inline zlink::message_t make_stream_reply_frame (const stream_server_frame_t &request, zlink::message_t payload)
{
    return make_stream_frame (zlink::stream_connector::message_kind_t::response, request.request_seq, "reply",
                              payload.to_string ());
}

inline zlink::message_t make_stream_push_frame (std::string packet_name, zlink::message_t payload)
{
    return make_stream_frame (zlink::stream_connector::message_kind_t::send, std::nullopt, std::move (packet_name),
                              payload.to_string ());
}

inline bool is_transient_stream_send_backpressure (const zlink::submit_error_t &error)
{
    return error.result () == zlink::submit_result_t::backpressured || error.internal_errno () == EAGAIN
           || error.internal_errno () == EWOULDBLOCK;
}

inline void send_stream_bytes (zlink::received_t &inbound, const std::string &bytes)
{
    constexpr int max_attempts = 200;
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        try {
            auto accepted = inbound.send ()
                              .message (zlink::message_t::from (bytes))
                              .flags (static_cast<int> (zlink::send_flags_t::dontwait))
                              .submit ();
            if (accepted) {
                return;
            }
        }
        catch (const zlink::submit_error_t &error) {
            if (!is_transient_stream_send_backpressure (error)) {
                throw;
            }
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    throw zlink::submit_error_t (zlink::submit_result_t::backpressured, EAGAIN);
}

inline void send_stream_frames (zlink::received_t &inbound, std::vector<zlink::message_t> frames)
{
    std::string bytes;
    for (const auto &frame : frames) {
        bytes += frame.to_string ();
    }
    send_stream_bytes (inbound, bytes);
}

template <typename TReply>
void send_stream_reply (zlink::received_t &inbound, const stream_server_frame_t &request, const TReply &reply)
{
    send_stream_frames (inbound, {make_stream_reply_frame (request, zlink::message_t::from_json (reply))});
}

template <typename TReply, typename TNotify>
void send_stream_reply_and_push (zlink::received_t &inbound,
                                 const stream_server_frame_t &request,
                                 const TReply &reply,
                                 std::string packet_name,
                                 const TNotify &notify)
{
    send_stream_frames (inbound,
                        {make_stream_reply_frame (request, zlink::message_t::from_json (reply)),
                         make_stream_push_frame (std::move (packet_name), zlink::message_t::from_json (notify))});
}

template <typename TNotify>
void send_stream_push (zlink::received_t &inbound, std::string packet_name, const TNotify &notify)
{
    send_stream_frames (inbound,
                        {make_stream_push_frame (std::move (packet_name), zlink::message_t::from_json (notify))});
}

} // namespace zlink::samples
