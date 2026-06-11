/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/stream_connector/codecs/auto_codec.hpp>
#include <zlink/stream_e2e_client/coroutine.hpp>

#include <chrono>
#include <functional>
#include <string>
#include <utility>

namespace zlink::stream_e2e_client::codecs
{

using zlink::stream_connector::metadata_t;
using zlink::stream_connector::result_t;
using zlink::stream_connector::codecs::codec_traits;
using zlink::stream_connector::codecs::encode_packet;

class coroutine_auto_send_call_t
{
  public:
    explicit coroutine_auto_send_call_t (coroutine_send_call_t inner) : _inner (std::move (inner))
    {
    }

    coroutine_auto_send_call_t &packet_name (std::string name)
    {
        _inner.packet_name (std::move (name));
        return *this;
    }

    coroutine_auto_send_call_t &metadata (std::string key, std::string value)
    {
        _inner.metadata (std::move (key), std::move (value));
        return *this;
    }

    coroutine_auto_send_call_t &metadata (metadata_t metadata)
    {
        _inner.metadata (std::move (metadata));
        return *this;
    }

    coroutine_auto_send_call_t &compress ()
    {
        _inner.compress ();
        return *this;
    }

    result_t<void> submit () { return _inner.submit (); }

    task_t<void> async () { return _inner.async (); }

  private:
    coroutine_send_call_t _inner;
};

template <typename TReply> class coroutine_auto_request_call_t
{
  public:
    explicit coroutine_auto_request_call_t (coroutine_request_call_t<zlink::message_t> inner) :
        _inner (std::move (inner))
    {
    }

    coroutine_auto_request_call_t &packet_name (std::string name)
    {
        _inner.packet_name (std::move (name));
        return *this;
    }

    coroutine_auto_request_call_t &metadata (std::string key, std::string value)
    {
        _inner.metadata (std::move (key), std::move (value));
        return *this;
    }

    coroutine_auto_request_call_t &metadata (metadata_t metadata)
    {
        _inner.metadata (std::move (metadata));
        return *this;
    }

    coroutine_auto_request_call_t &timeout (std::chrono::milliseconds timeout)
    {
        _inner.timeout (timeout);
        return *this;
    }

    coroutine_auto_request_call_t &compress ()
    {
        _inner.compress ();
        return *this;
    }

    result_t<TReply> submit ()
    {
        auto result = _inner.submit ();
        if (!result) {
            return result_t<TReply>::failure (
              result.error_code (),
              result.error () ? result.error ()->message : "stream request failed");
        }
        return result_t<TReply>::success (codec_traits<TReply>::decode (result.value ()));
    }

    task_t<TReply> async () { return task_t<TReply> (submit ()); }

  private:
    coroutine_request_call_t<zlink::message_t> _inner;
};

template <typename T>
coroutine_auto_send_call_t send (coroutine_connector_t &connector, const T &payload)
{
    return coroutine_auto_send_call_t (connector.send (encode_packet (payload)));
}

template <typename TReply, typename TRequest>
coroutine_auto_request_call_t<TReply> request (coroutine_connector_t &connector,
                                               const TRequest &payload)
{
    return coroutine_auto_request_call_t<TReply> (
      connector.request<zlink::message_t> (encode_packet (payload)));
}

} // namespace zlink::stream_e2e_client::codecs
