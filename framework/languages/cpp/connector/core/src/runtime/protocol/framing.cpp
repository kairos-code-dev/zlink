/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/protocol/framing.hpp"

#include "runtime/protocol/compression/lz4_compression_codec.hpp"
#include "runtime/protocol/framing/frame_codec.hpp"
#include "runtime/protocol/header_codec.hpp"
#include "runtime/transport/stream_connection.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>

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

void publish_observer_error (const std::vector<std::function<void (const error_t &)>> &handlers,
                             const error_t &error) noexcept
{
    for (const auto &handler : handlers) {
        try {
            handler (error);
        }
        catch (...) {
        }
    }
}

void enqueue_inbound_observer_notification (connector_state_t &state,
                                            const stream_header_t &header,
                                            std::size_t payload_length,
                                            const std::vector<std::uint8_t> &payload_bytes)
{
    std::vector<std::shared_ptr<inbound_observer_entry_t>> observers;
    for (const auto &observer : state.inbound_observers) {
        if (observer && observer->active.load () && observer->callback) {
            observers.push_back (observer);
        }
    }
    if (observers.empty ()) {
        return;
    }
    const auto pending =
      state.pending_inbound_observer_notifications.fetch_add (1) + static_cast<std::size_t> (1);
    if (pending > state.options.max_inbound_observer_notifications) {
        state.pending_inbound_observer_notifications.fetch_sub (1);
        if (!state.inbound_observer_drop_report_pending.exchange (true)) {
            publish_observer_error (
              state.error_handlers,
              error_t{
                error_code_t::observer_dropped,
                "Inbound observer notification was dropped because the observer queue is full."});
            state.inbound_observer_drop_report_pending.store (false);
        }
        return;
    }
    auto error_handlers = state.error_handlers;
    inbound_observation_t observation;
    observation.kind = header.kind;
    observation.name = header.name;
    observation.codec = header.codec;
    observation.request_seq = header.request_seq;
    observation.metadata = header.metadata;
    observation.payload_length = payload_length;
    observation.compressed = has_flag (header.flags, header_flags_t::payload_compressed);
    observation.received_at = std::chrono::steady_clock::now ();
    const auto preview_length =
      std::min (state.options.max_inbound_observer_payload_preview_bytes, payload_bytes.size ());
    observation.payload_preview.assign (payload_bytes.begin (),
                                        payload_bytes.begin ()
                                          + static_cast<std::ptrdiff_t> (preview_length));
    auto shared_state = state.shared_from_this ();
    post_runtime_operation ([state = std::move (shared_state), observers = std::move (observers),
                             error_handlers = std::move (error_handlers),
                             observation = std::move (observation)] () mutable {
        for (const auto &observer : observers) {
            if (!observer || !observer->active.load ()) {
                continue;
            }
            try {
                observer->callback (observation);
            }
            catch (const std::exception &ex) {
                publish_observer_error (error_handlers, error_t{error_code_t::observer_failed,
                                                                "Inbound observer callback failed: "
                                                                  + std::string (ex.what ())});
            }
            catch (...) {
                publish_observer_error (
                  error_handlers,
                  error_t{error_code_t::observer_failed, "Inbound observer callback failed."});
            }
        }
        state->pending_inbound_observer_notifications.fetch_sub (1);
    });
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
    enqueue_inbound_observer_notification (state, header, payload_size, payload_bytes);
    state.last_inbound_received = std::chrono::steady_clock::now ();
    const bool compressed = has_flag (header.flags, header_flags_t::payload_compressed);
    auto payload = message_from_bytes (payload_bytes);
    if (compressed) {
        if (!state.compression_codec) {
            throw std::runtime_error ("stream connector compression codec is not configured");
        }
        if (state.options.compression == compression_t::lz4 && !state.lz4_enabled) {
            throw std::runtime_error ("LZ4 compression is not enabled");
        }
        payload = state.compression_codec->decompress (payload,
                                                       state.options.max_receive_payload_size);
        if (payload.size () > state.options.max_receive_payload_size) {
            throw std::runtime_error (
              "decompressed stream payload exceeds maximum stream payload size");
        }
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
