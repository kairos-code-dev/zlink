/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/codec/json.hpp>
#include <zlink/stream_connector/contracts/connector.hpp>
#include <zlink/stream_connector/contracts/stream_payload.hpp>

#include <chrono>
#include <functional>
#include <string>
#include <utility>

namespace zlink::stream_connector::codecs {

    template<typename T>
    struct codec_traits {
        /// Codec used when T is sent or received through the auto codec helpers.
        static constexpr codec_t codec = codec_t::json;

        /// Encodes a typed value into a stream payload.
        static zlink::message_t encode(const T &value) { return zlink::message_t::from_json(value); }

        /// Decodes a stream payload into a typed value.
        static T decode(const zlink::message_t &message) { return message.parse_json<T>(); }
    };

    /// Encodes a typed value as a packet using codec_traits<T>.
    template<typename T>
    packet_t encode_packet(const T &value) {
        packet_t packet;
        packet.name = detail::message_packet_name<T>();
        packet.codec = codec_traits<T>::codec;
        packet.payload = codec_traits<T>::encode(value);
        return packet;
    }

    class auto_send_call_t {
    public:
        explicit auto_send_call_t(send_call_t inner) : _inner(std::move(inner)) {
        }

        /// Overrides the packet name sent with this call.
        auto_send_call_t &packet_name(std::string name) {
            _inner.packet_name(std::move(name));
            return *this;
        }

        /// Adds or replaces one metadata value copied into the outbound packet.
        auto_send_call_t &metadata(std::string key, std::string value) {
            _inner.metadata(std::move(key), std::move(value));
            return *this;
        }

        /// Replaces the outbound packet metadata.
        auto_send_call_t &metadata(metadata_t metadata) {
            _inner.metadata(std::move(metadata));
            return *this;
        }

        /// Marks the outbound packet for compression when compression is available.
        auto_send_call_t &compress() {
            _inner.compress();
            return *this;
        }

        /// Sends the encoded packet and blocks until the send result is known.
        result_t<void> submit() { return _inner.submit(); }

        /// Sends the encoded packet as an awaitable task.
        task_t<void> submit_async() { return _inner.submit_async(); }

        /// Sends the encoded packet and invokes the callback with the completion result.
        void submit_async(std::function<void (result_t<void>)> callback) {
            _inner.submit_async(std::move(callback));
        }

    private:
        send_call_t _inner;
    };

    template<typename TReply>
    class auto_request_call_t {
    public:
        explicit auto_request_call_t(request_call_t<zlink::message_t> inner) : _inner(std::move(inner)) {
        }

        /// Overrides the packet name sent with this request.
        auto_request_call_t &packet_name(std::string name) {
            _inner.packet_name(std::move(name));
            return *this;
        }

        /// Adds or replaces one metadata value copied into the outbound request packet.
        auto_request_call_t &metadata(std::string key, std::string value) {
            _inner.metadata(std::move(key), std::move(value));
            return *this;
        }

        /// Replaces the outbound request metadata.
        auto_request_call_t &metadata(metadata_t metadata) {
            _inner.metadata(std::move(metadata));
            return *this;
        }

        /// Sets the request timeout used by submit and submit_async.
        auto_request_call_t &timeout(std::chrono::milliseconds timeout) {
            _inner.timeout(timeout);
            return *this;
        }

        /// Marks the outbound request packet for compression when compression is available.
        auto_request_call_t &compress() {
            _inner.compress();
            return *this;
        }

        /// Sends the encoded request, waits for the correlated reply, and decodes it as TReply.
        result_t<TReply> submit() {
            auto result = _inner.submit();
            if (!result) {
                return result_t<TReply>::failure(
                    result.error_code(),
                    result.error() ? result.error()->message : "stream request failed");
            }
            return result_t<TReply>::success(codec_traits<TReply>::decode(result.value()));
        }

        /// Sends the encoded request and returns an awaitable decoded reply.
        task_t<TReply> submit_async() { return task_t<TReply>(submit()); }

        /// Sends the encoded request and invokes the callback with the decoded reply result.
        void submit_async(std::function<void (result_t<TReply>)> callback) {
            auto task = submit_async();
            task.on_completed(std::move(callback));
        }

    private:
        request_call_t<zlink::message_t> _inner;
    };

    /// Starts a typed send call using codec_traits<T>.
    template<typename T>
    auto_send_call_t send(connector_t &connector, const T &payload) {
        return auto_send_call_t(connector.send(encode_packet(payload)));
    }

    /// Starts a typed request call using codec_traits<TRequest> for the request payload.
    template<typename TReply, typename TRequest>
    auto_request_call_t<TReply> request(connector_t &connector, const TRequest &payload) {
        return auto_request_call_t<TReply>(
            connector.request<zlink::message_t>(encode_packet(payload)));
    }

    /// Registers a typed packet callback for the given packet name.
    template<typename T>
    connector_t &
    on(connector_t &connector, std::string packet_name, std::function<void (const T &)> callback) {
        return connector.on<packet_t>(std::move(packet_name),
                                      [callback = std::move(callback)](const packet_t &packet) {
                                          callback(codec_traits<T>::decode(packet.payload));
                                      });
    }

    /// Registers a typed packet callback for the packet name resolved from T.
    template<typename T>
    connector_t &on(connector_t &connector, std::function<void (const T &)> callback) {
        return on<T>(connector, detail::message_packet_name<T>(), std::move(callback));
    }

}
