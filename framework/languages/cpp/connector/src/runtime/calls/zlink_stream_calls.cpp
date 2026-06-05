/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/connector_runtime.hpp"

#include "runtime/heartbeat_monitor.hpp"
#include "runtime/protocol/compression/lz4_compression_codec.hpp"
#include "runtime/protocol/framing/frame_codec.hpp"
#include "runtime/protocol/framing.hpp"
#include "runtime/protocol/header_codec.hpp"
#include "runtime/transport/stream_connection.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>

namespace zlink::stream_connector::detail
{

namespace
{

result_t<void> validate_packet_limits (const connector_state_t &state, const packet_t &packet)
{
    if (packet.payload.size () > state.options.max_send_payload_size) {
        return result_t<void>::failure (error_code_t::frame_too_large, "stream connector payload is too large");
    }
    std::size_t metadata_size = 0;
    for (const auto &[key, value] : packet.metadata.values) {
        metadata_size += key.size () + value.size ();
    }
    if (metadata_size > state.options.max_metadata_size) {
        return result_t<void>::failure (error_code_t::validation_failed, "stream connector metadata is too large");
    }
    if (packet.codec == codec_t::message_pack && !state.message_pack_enabled) {
        return result_t<void>::failure (error_code_t::unsupported_codec, "MessagePack codec is not enabled");
    }
    if (packet.codec == codec_t::protobuf && !state.protobuf_enabled) {
        return result_t<void>::failure (error_code_t::unsupported_codec, "Protobuf codec is not enabled");
    }
    if (packet.compressed) {
        if (state.options.compression != compression_t::lz4) {
            return result_t<void>::failure (error_code_t::compression_failed,
                                            "stream connector compression is not configured");
        }
        if (!state.lz4_enabled) {
            return result_t<void>::failure (error_code_t::compression_failed, "LZ4 compression is not enabled");
        }
    }
    return result_t<void>::success ();
}

void publish_error (const connector_state_t &state, const error_t &error)
{
    for (const auto &handler : state.error_handlers) {
        handler (error);
    }
}

std::vector<std::uint8_t> message_to_bytes (const zlink::message_t &message)
{
    return message.to_bytes ();
}

zlink::message_t message_from_bytes (const std::vector<std::uint8_t> &bytes)
{
    return zlink::message_t::from (bytes);
}

bool has_flag (header_flags_t flags, header_flags_t flag) noexcept
{
    return (static_cast<std::uint8_t> (flags) & static_cast<std::uint8_t> (flag)) != 0;
}

using steady_clock_t = std::chrono::steady_clock;

result_t<std::vector<std::uint8_t>>
read_exact_until (connector_state_t &state, std::size_t size, steady_clock_t::time_point deadline)
{
    std::vector<std::uint8_t> bytes (size);
    std::size_t offset = 0;
    while (offset < size) {
        if (steady_clock_t::now () >= deadline) {
            return result_t<std::vector<std::uint8_t>>::failure (error_code_t::request_timeout,
                                                                 "stream connector request timed out");
        }
        boost::system::error_code error;
        const auto available = state.connection ? state.connection->available (error) : 0;
        if (error) {
            return result_t<std::vector<std::uint8_t>>::failure (error_code_t::disconnected, error.message ());
        }
        if (available == 0) {
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
            continue;
        }
        const auto to_read = std::min<std::size_t> (available, bytes.size () - offset);
        const auto read = state.connection->read_some (bytes.data () + offset, to_read, error);
        if (error) {
            return result_t<std::vector<std::uint8_t>>::failure (error_code_t::disconnected, error.message ());
        }
        offset += read;
    }
    return result_t<std::vector<std::uint8_t>>::success (std::move (bytes));
}

result_t<void> write_packet_frame (connector_state_t &state,
                                   message_kind_t kind,
                                   const packet_t &packet,
                                   std::optional<std::uint64_t> request_seq)
{
    header_flags_t flags = header_flags_t::none;
    if (packet.compressed) {
        flags = flags | header_flags_t::payload_compressed;
    }
    header_codec_t header_codec;
    auto header =
      header_codec.encode (stream_header_t{kind, packet.codec, flags, request_seq, packet.name, packet.metadata});
    if (!header) {
        return result_t<void>::failure (header.error_code (), header.error ()->message);
    }
    const zlink::message_t *payload_message = &packet.payload;
    std::optional<zlink::message_t> compressed_payload;
    if (packet.compressed && state.options.compression == compression_t::lz4) {
        try {
            compressed_payload = lz4_compression_codec_t{}.compress (packet.payload);
            payload_message = &*compressed_payload;
        }
        catch (const std::exception &ex) {
            return result_t<void>::failure (error_code_t::compression_failed, ex.what ());
        }
    }
    auto payload = message_to_bytes (*payload_message);
    auto frame = frame_codec_t::encode (header.value (), payload, state.options);
    if (!frame) {
        return result_t<void>::failure (frame.error_code (), frame.error ()->message);
    }
    write_bytes (state, frame.value ());
    return result_t<void>::success ();
}

result_t<packet_t> read_packet_frame (connector_state_t &state, steady_clock_t::time_point deadline)
{
    auto prefix_result = read_exact_until (state, 6, deadline);
    if (!prefix_result) {
        return result_t<packet_t>::failure (prefix_result.error_code (), prefix_result.error ()
                                                                           ? prefix_result.error ()->message
                                                                           : "stream connector read failed");
    }
    auto prefix = std::move (prefix_result.value ());
    const auto header_size = static_cast<std::size_t> ((prefix[0] << 8) | prefix[1]);
    const auto payload_size = (static_cast<std::size_t> (prefix[2]) << 24)
                              | (static_cast<std::size_t> (prefix[3]) << 16)
                              | (static_cast<std::size_t> (prefix[4]) << 8) | static_cast<std::size_t> (prefix[5]);
    auto header_bytes = read_exact_until (state, header_size, deadline);
    if (!header_bytes) {
        return result_t<packet_t>::failure (header_bytes.error_code (), header_bytes.error ()
                                                                          ? header_bytes.error ()->message
                                                                          : "stream connector header read failed");
    }
    auto payload_bytes = read_exact_until (state, payload_size, deadline);
    if (!payload_bytes) {
        return result_t<packet_t>::failure (payload_bytes.error_code (), payload_bytes.error ()
                                                                           ? payload_bytes.error ()->message
                                                                           : "stream connector payload read failed");
    }
    header_codec_t header_codec;
    auto decoded = header_codec.decode (header_bytes.value ());
    if (!decoded) {
        return result_t<packet_t>::failure (decoded.error_code (), decoded.error ()->message);
    }
    auto header = decoded.value ();
    auto payload = message_from_bytes (payload_bytes.value ());
    state.last_inbound_received = steady_clock_t::now ();
    const bool compressed = has_flag (header.flags, header_flags_t::payload_compressed);
    if (compressed) {
        if (state.options.compression != compression_t::lz4) {
            return result_t<packet_t>::failure (error_code_t::decompression_failed,
                                                "stream connector compression is not configured");
        }
        if (!state.lz4_enabled) {
            return result_t<packet_t>::failure (error_code_t::decompression_failed, "LZ4 compression is not enabled");
        }
        try {
            payload = lz4_compression_codec_t{}.decompress (payload);
        }
        catch (const std::exception &ex) {
            return result_t<packet_t>::failure (error_code_t::decompression_failed, ex.what ());
        }
    }
    return result_t<packet_t>::success (packet_t{header.name, header.metadata, header.codec, compressed, payload});
}

result_t<void> send_due_heartbeat (connector_state_t &state)
{
    if (!is_transport_connected (state)) {
        return result_t<void>::success ();
    }
    const auto now = steady_clock_t::now ();
    heartbeat_monitor_t monitor (state.options.heartbeat);
    if (!monitor.due (state.last_heartbeat_sent, now)) {
        return result_t<void>::success ();
    }
    packet_t heartbeat;
    heartbeat.name = "$zlink.heartbeat.ping";
    heartbeat.codec = codec_t::raw;
    heartbeat.payload = zlink::message_t::from (std::string{});
    auto written = write_packet_frame (state, message_kind_t::control, heartbeat, std::nullopt);
    if (written) {
        state.last_heartbeat_sent = now;
    }
    return written;
}

} // namespace

result_t<void> submit_send (std::shared_ptr<connector_state_t> state, packet_t packet)
{
    std::lock_guard<std::mutex> lock (state->transport_mutex);
    if (!is_transport_connected (*state)) {
        return result_t<void>::failure (error_code_t::disconnected, "stream connector is not connected");
    }
    if (auto validation = validate_packet_limits (*state, packet); !validation) {
        publish_error (*state, *validation.error ());
        return validation;
    }
    if (auto written = write_packet_frame (*state, message_kind_t::send, packet, std::nullopt); !written) {
        publish_error (*state, *written.error ());
        return written;
    }
    state->sent_packets.push_back (std::move (packet));
    return result_t<void>::success ();
}

result_t<zlink::message_t>
submit_request (std::shared_ptr<void> state_handle, packet_t packet, std::chrono::milliseconds timeout)
{
    auto state = std::static_pointer_cast<connector_state_t> (std::move (state_handle));
    std::lock_guard<std::mutex> lock (state->transport_mutex);
    if (!is_transport_connected (*state)) {
        return result_t<zlink::message_t>::failure (error_code_t::disconnected, "stream connector is not connected");
    }
    if (auto validation = validate_packet_limits (*state, packet); !validation) {
        publish_error (*state, *validation.error ());
        return result_t<zlink::message_t>::failure (validation.error_code (), validation.error ()
                                                                                ? validation.error ()->message
                                                                                : "stream request validation failed");
    }
    const auto seq = state->next_request_seq++;
    state->pending_requests.emplace (seq, pending_request_t{seq, std::move (packet)});
    const auto &request_packet = state->pending_requests.at (seq).packet;
    if (auto written = write_packet_frame (*state, message_kind_t::request, request_packet, seq); !written) {
        publish_error (*state, *written.error ());
        state->pending_requests.erase (seq);
        return result_t<zlink::message_t>::failure (written.error_code (), written.error ()->message);
    }
    const auto deadline = steady_clock_t::now () + timeout;
    for (;;) {
        auto received = read_packet_frame (*state, deadline);
        if (!received) {
            state->pending_requests.erase (seq);
            return result_t<zlink::message_t>::failure (received.error_code (), received.error ()->message);
        }
        auto packet = std::move (received.value ());
        if (packet.name != "reply") {
            deliver_received_packet (*state, std::move (packet));
            continue;
        }
        state->pending_requests.erase (seq);
        return result_t<zlink::message_t>::success (packet.payload);
    }
}

result_t<void> dispatch_pending (std::shared_ptr<connector_state_t> state)
{
    std::lock_guard<std::mutex> lock (state->transport_mutex);
    if (is_transport_connected (*state)) {
        drain_available_pushes (*state);
        heartbeat_monitor_t monitor (state->options.heartbeat);
        const auto now = steady_clock_t::now ();
        if (monitor.timed_out (state->last_inbound_received, now)) {
            boost::system::error_code ignored;
            if (state->connection) {
                state->connection->close (ignored);
            }
            error_t error{error_code_t::disconnected, "stream connector heartbeat timed out"};
            publish_error (*state, error);
            change_state (state, connection_state_t::disconnected, error);
            return result_t<void>::failure (error.code, error.message);
        }
        if (auto heartbeat = send_due_heartbeat (*state); !heartbeat) {
            return heartbeat;
        }
    }
    while (!state->dispatch_queue.empty ()) {
        auto packet = std::move (state->dispatch_queue.front ());
        state->dispatch_queue.pop_front ();
        dispatch_packet (*state, packet);
    }
    return result_t<void>::success ();
}

result_t<packet_t> receive_next (std::shared_ptr<connector_state_t> state, std::chrono::milliseconds timeout)
{
    const auto deadline = steady_clock_t::now () + timeout;
    for (;;) {
        {
            std::lock_guard<std::mutex> lock (state->transport_mutex);
            if (is_transport_connected (*state)) {
                drain_available_pushes (*state);
            }
            if (!state->dispatch_queue.empty ()) {
                auto packet = std::move (state->dispatch_queue.front ());
                state->dispatch_queue.pop_front ();
                return result_t<packet_t>::success (std::move (packet));
            }
            if (!is_transport_connected (*state)) {
                return result_t<packet_t>::failure (error_code_t::disconnected, "stream connector is not connected");
            }
        }

        if (steady_clock_t::now () >= deadline) {
            return result_t<packet_t>::failure (error_code_t::request_timeout, "stream connector receive timed out");
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
}

} // namespace zlink::stream_connector::detail
