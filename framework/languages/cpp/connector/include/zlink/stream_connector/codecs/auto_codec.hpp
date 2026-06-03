/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/codec/json.hpp>
#include <zlink/stream_connector/contracts/connector.hpp>
#include <zlink/stream_connector/contracts/stream_payload.hpp>

#include <chrono>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>

namespace zlink::stream_connector::codecs
{

template<typename T>
struct codec_traits
{
  static constexpr codec_t codec = codec_t::json;

  static zlink::message_t encode (const T &value)
  {
    return zlink::message_t::from_json (value);
  }

  static T decode (const zlink::message_t &message)
  {
    return message.template parse_json<T> ();
  }
};

template<typename T>
packet_t encode_packet (const T &value)
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
  explicit auto_send_call_t (send_call_t inner)
    : _inner (std::move (inner))
  {
  }

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

  task_t<void> submit ()
  {
    return _inner.submit ();
  }

  void submit (std::function<void (result_t<void>)> callback)
  {
    _inner.submit (std::move (callback));
  }

private:
  send_call_t _inner;
};

template<typename TReply>
class auto_request_call_t
{
public:
  explicit auto_request_call_t (request_call_t<zlink::message_t> inner)
    : _inner (std::move (inner))
  {
  }

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

  task_t<TReply> submit ()
  {
    auto result = _inner.submit ().consume_result ();
    if (!result) {
      return task_t<TReply> (result_t<TReply>::failure (
        result.error_code (),
        result.error () ? result.error ()->message : "stream request failed"));
    }
    return task_t<TReply> (
      result_t<TReply>::success (codec_traits<TReply>::decode (result.value ())));
  }

  void submit (std::function<void (result_t<TReply>)> callback)
  {
    auto task = submit ();
    task.on_completed (std::move (callback));
  }

private:
  request_call_t<zlink::message_t> _inner;
};

template<typename T>
auto_send_call_t
send (connector_t &connector, const T &payload)
{
  return auto_send_call_t (connector.send (encode_packet (payload)));
}

template<typename TReply, typename TRequest>
auto_request_call_t<TReply>
request (connector_t &connector, const TRequest &payload)
{
  return auto_request_call_t<TReply> (
    connector.request<zlink::message_t> (encode_packet (payload)));
}

template<typename T>
connector_t &
on (connector_t &connector,
    std::string packet_name,
    std::function<void (const T &)> callback)
{
  return connector.on<packet_t> (
    std::move (packet_name),
    [callback = std::move (callback)](const packet_t &packet) {
      callback (codec_traits<T>::decode (packet.payload));
    });
}

template<typename T>
connector_t &
on (connector_t &connector, std::function<void (const T &)> callback)
{
  return on<T> (connector,
                detail::message_packet_name<T> (),
                std::move (callback));
}

} // namespace zlink::stream_connector::codecs
