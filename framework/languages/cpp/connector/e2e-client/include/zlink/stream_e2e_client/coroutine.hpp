/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/stream_connector.hpp>
#include <zlink/stream_e2e_client/task.hpp>

#include <chrono>
#include <functional>
#include <string>
#include <utility>

namespace zlink::stream_e2e_client
{

using zlink::stream_connector::codec_t;
using zlink::stream_connector::connector_t;
using zlink::stream_connector::metadata_t;
using zlink::stream_connector::packet_t;
using zlink::stream_connector::request_call_t;
using zlink::stream_connector::result_t;
using zlink::stream_connector::send_call_t;
using zlink::stream_connector::wait_call_t;

class coroutine_send_call_t
{
  public:
    explicit coroutine_send_call_t (send_call_t inner) : _inner (std::move (inner)) {}

    coroutine_send_call_t &packet_name (std::string name)
    {
        _inner.packet_name (std::move (name));
        return *this;
    }

    coroutine_send_call_t &metadata (std::string key, std::string value)
    {
        _inner.metadata (std::move (key), std::move (value));
        return *this;
    }

    coroutine_send_call_t &metadata (metadata_t metadata)
    {
        _inner.metadata (std::move (metadata));
        return *this;
    }

    coroutine_send_call_t &codec (codec_t codec)
    {
        _inner.codec (codec);
        return *this;
    }

    coroutine_send_call_t &compress ()
    {
        _inner.compress ();
        return *this;
    }

    result_t<void> submit () { return _inner.submit (); }

    task_t<void> async () { return task_t<void> (submit ()); }

  private:
    send_call_t _inner;
};

template <typename TReply> class coroutine_request_call_t
{
  public:
    explicit coroutine_request_call_t (request_call_t<TReply> inner) :
        _inner (std::move (inner))
    {
    }

    coroutine_request_call_t &packet_name (std::string name)
    {
        _inner.packet_name (std::move (name));
        return *this;
    }

    coroutine_request_call_t &metadata (std::string key, std::string value)
    {
        _inner.metadata (std::move (key), std::move (value));
        return *this;
    }

    coroutine_request_call_t &metadata (metadata_t metadata)
    {
        _inner.metadata (std::move (metadata));
        return *this;
    }

    coroutine_request_call_t &codec (codec_t codec)
    {
        _inner.codec (codec);
        return *this;
    }

    coroutine_request_call_t &timeout (std::chrono::milliseconds timeout)
    {
        _inner.timeout (timeout);
        return *this;
    }

    coroutine_request_call_t &compress ()
    {
        _inner.compress ();
        return *this;
    }

    result_t<TReply> submit () { return _inner.submit (); }

    task_t<TReply> async () { return task_t<TReply> (submit ()); }

  private:
    request_call_t<TReply> _inner;
};

template <typename TMessage> class coroutine_wait_call_t
{
  public:
    explicit coroutine_wait_call_t (wait_call_t<TMessage> inner) : _inner (std::move (inner)) {}

    coroutine_wait_call_t &packet_name (std::string name)
    {
        _inner.packet_name (std::move (name));
        return *this;
    }

    coroutine_wait_call_t &timeout (std::chrono::milliseconds timeout)
    {
        _inner.timeout (timeout);
        return *this;
    }

    coroutine_wait_call_t &where (std::function<bool (const TMessage &)> predicate)
    {
        _inner.where (std::move (predicate));
        return *this;
    }

    result_t<TMessage> submit () { return _inner.submit (); }

    task_t<TMessage> async () { return task_t<TMessage> (submit ()); }

  private:
    wait_call_t<TMessage> _inner;
};

class coroutine_connector_t
{
  public:
    explicit coroutine_connector_t (connector_t &connector) : _connector (&connector) {}

    template <typename TMessage> coroutine_send_call_t send (const TMessage &message)
    {
        return coroutine_send_call_t (_connector->send (message));
    }

    coroutine_send_call_t send (packet_t packet)
    {
        return coroutine_send_call_t (_connector->send (std::move (packet)));
    }

    template <typename TReply, typename TRequest>
    coroutine_request_call_t<TReply> request (const TRequest &request)
    {
        return coroutine_request_call_t<TReply> (_connector->request<TReply> (request));
    }

    template <typename TReply> coroutine_request_call_t<TReply> request (packet_t packet)
    {
        return coroutine_request_call_t<TReply> (_connector->request<TReply> (std::move (packet)));
    }

    template <typename TMessage> coroutine_wait_call_t<TMessage> wait_for ()
    {
        return coroutine_wait_call_t<TMessage> (_connector->wait_for<TMessage> ());
    }

    template <typename TMessage>
    coroutine_wait_call_t<TMessage> wait_for (std::chrono::milliseconds timeout)
    {
        return coroutine_wait_call_t<TMessage> (_connector->wait_for<TMessage> (timeout));
    }

    template <typename TMessage> coroutine_wait_call_t<TMessage> wait_for (std::string packet_name)
    {
        return coroutine_wait_call_t<TMessage> (
          _connector->wait_for<TMessage> (std::move (packet_name)));
    }

    template <typename TMessage>
    coroutine_wait_call_t<TMessage> wait_for (std::string packet_name,
                                             std::chrono::milliseconds timeout)
    {
        return coroutine_wait_call_t<TMessage> (
          _connector->wait_for<TMessage> (std::move (packet_name), timeout));
    }

  private:
    connector_t *_connector;
};

inline coroutine_connector_t coroutine (connector_t &connector)
{
    return coroutine_connector_t (connector);
}

inline coroutine_connector_t use (connector_t &connector)
{
    return coroutine (connector);
}

} // namespace zlink::stream_e2e_client
