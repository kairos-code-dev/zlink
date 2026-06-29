/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/codecs/json.hpp>
#include <zlink/stream_connector/contracts/connector.hpp>
#include <zlink/stream_connector/contracts/stream_payload.hpp>

#include <chrono>
#include <functional>
#include <string>
#include <utility>

namespace zlink::stream_connector::codecs
{

template <typename T> struct codec_traits
{
    static constexpr codec_t codec = codec_t::json;

    static zlink::message_t encode (const T &value) { return zlink::message_t::from_json (value); }

    static T decode (const zlink::message_t &message) { return message.parse_json<T> (); }
};

template <typename T> void decode_payload (codec_t codec, const zlink::message_t &payload, T &value)
{
    switch (codec) {
        case codec_t::json:
        case codec_t::raw:
        case codec_t::protobuf:
            value = codec_traits<T>::decode (payload);
            return;
        case codec_t::message_pack:
            value = nlohmann::json::from_msgpack (payload.to_bytes ()).template get<T> ();
            return;
    }
    value = codec_traits<T>::decode (payload);
}

template <typename T> packet_t encode_packet (const T &value)
{
    packet_t packet;
    packet.name = detail::message_packet_name<T> ();
    packet.codec = codec_traits<T>::codec;
    packet.payload = codec_traits<T>::encode (value);
    return packet;
}

class auto_send_call_t
{
  public:
    explicit auto_send_call_t (send_call_t inner) : _inner (std::move (inner)) {}

    auto_send_call_t &packet_name (std::string name)
    {
        _inner.packet_name (std::move (name));
        return *this;
    }

    auto_send_call_t &metadata (std::string key, std::string value)
    {
        _inner.metadata (std::move (key), std::move (value));
        return *this;
    }

    auto_send_call_t &metadata (metadata_t metadata)
    {
        _inner.metadata (std::move (metadata));
        return *this;
    }

    auto_send_call_t &compress ()
    {
        _inner.compress ();
        return *this;
    }

    result_t<void> submit () { return _inner.submit (); }

    void submit (std::function<void (result_t<void>)> callback)
    {
        _inner.submit (std::move (callback));
    }

  private:
    send_call_t _inner;
};

class auto_request_call_t
{
  public:
    explicit auto_request_call_t (request_call_t inner) : _inner (std::move (inner)) {}

    auto_request_call_t &packet_name (std::string name)
    {
        _inner.packet_name (std::move (name));
        return *this;
    }

    auto_request_call_t &metadata (std::string key, std::string value)
    {
        _inner.metadata (std::move (key), std::move (value));
        return *this;
    }

    auto_request_call_t &metadata (metadata_t metadata)
    {
        _inner.metadata (std::move (metadata));
        return *this;
    }

    auto_request_call_t &timeout (std::chrono::milliseconds timeout)
    {
        _inner.timeout (timeout);
        return *this;
    }

    auto_request_call_t &compress ()
    {
        _inner.compress ();
        return *this;
    }

    template <typename TReply> result_t<TReply> submit ()
    {
        auto result = _inner.template submit<zlink::message_t> ();
        if (!result) {
            return result_t<TReply>::failure (result.error_code (), result.error ()
                                                                      ? result.error ()->message
                                                                      : "stream request failed");
        }
        return result_t<TReply>::success (codec_traits<TReply>::decode (result.value ()));
    }

    template <typename TReply> void submit (std::function<void (result_t<TReply>)> callback)
    {
        _inner.template submit<zlink::message_t> (
          [callback = std::move (callback)] (result_t<zlink::message_t> result) mutable {
              if (!callback) {
                  return;
              }
              if (!result) {
                  callback (result_t<TReply>::failure (result.error_code (),
                                                       result.error () ? result.error ()->message
                                                                       : "stream request failed"));
                  return;
              }
              callback (result_t<TReply>::success (codec_traits<TReply>::decode (result.value ())));
          });
    }

  private:
    request_call_t _inner;
};

template <typename T> auto_send_call_t send (connector_t &connector, const T &payload)
{
    return auto_send_call_t (connector.send (encode_packet (payload)));
}

template <typename TRequest>
auto_request_call_t request (connector_t &connector, const TRequest &payload)
{
    return auto_request_call_t (connector.request (encode_packet (payload)));
}

template <typename T>
connector_t &
on (connector_t &connector, std::string packet_name, std::function<void (const T &)> callback)
{
    return connector.on<packet_t> (std::move (packet_name),
                                   [callback = std::move (callback)] (const packet_t &packet) {
                                       T value{};
                                       decode_payload (packet.codec, packet.payload, value);
                                       callback (std::move (value));
                                   });
}

template <typename T>
connector_t &on (connector_t &connector, std::function<void (const T &)> callback)
{
    return on<T> (connector, detail::message_packet_name<T> (), std::move (callback));
}

} // namespace zlink::stream_connector::codecs

namespace zlink::stream_connector
{

template <typename T>
void from_stream_payload (codec_t codec, const zlink::message_t &payload, T &value)
{
    codecs::decode_payload (codec, payload, value);
}

} // namespace zlink::stream_connector
