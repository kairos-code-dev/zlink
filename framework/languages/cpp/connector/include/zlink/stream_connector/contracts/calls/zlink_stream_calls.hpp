/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>
#include <zlink/stream_connector/contracts/stream_payload.hpp>
#include <zlink/stream_connector/contracts/task.hpp>
#include <zlink/stream_connector/contracts/zlink_stream_connector_options.hpp>
#include <zlink/stream_connector/contracts/zlink_stream_interfaces.hpp>
#include <zlink/stream_connector/contracts/zlink_stream_models.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace zlink::stream_connector
{

namespace detail
{
result_t<zlink::message_t> submit_request (
  std::shared_ptr<connector_state_t> state,
  packet_t packet,
  std::chrono::milliseconds timeout);
} // namespace detail

class send_call_t
{
public:
  send_call_t ();
  ~send_call_t ();

  send_call_t (send_call_t &&) noexcept;
  send_call_t &operator= (send_call_t &&) noexcept;
  send_call_t (const send_call_t &) = default;
  send_call_t &operator= (const send_call_t &) = default;

  send_call_t &packet_name (std::string name);
  send_call_t &metadata (std::string key, std::string value);
  send_call_t &metadata (metadata_t metadata);
  send_call_t &codec (codec_t codec);
  send_call_t &compress ();

  task_t<void> submit ();
  void submit (std::function<void (result_t<void>)> callback);

private:
  friend class connector_t;
  send_call_t (std::shared_ptr<detail::connector_state_t> state,
               packet_t packet);

  std::shared_ptr<detail::connector_state_t> _state;
  packet_t _packet;
};

template<typename TReply>
class request_call_t
{
public:
  request_call_t () = default;

  request_call_t &packet_name (std::string name)
  {
    _packet.name = std::move (name);
    return *this;
  }

  request_call_t &metadata (std::string key, std::string value)
  {
    _packet.metadata.with (std::move (key), std::move (value));
    return *this;
  }

  request_call_t &metadata (metadata_t metadata)
  {
    _packet.metadata = std::move (metadata);
    return *this;
  }

  request_call_t &codec (codec_t codec)
  {
    _packet.codec = codec;
    return *this;
  }

  request_call_t &timeout (std::chrono::milliseconds timeout)
  {
    _timeout = timeout;
    return *this;
  }

  request_call_t &compress ()
  {
    _packet.compressed = true;
    return *this;
  }

  task_t<TReply> submit ()
  {
    if (!_state) {
      return task_t<TReply> (result_t<TReply>::failure (
        error_code_t::configuration_error, "request call has no connector"));
    }
    return task_t<TReply> (submit_erased ().template as<TReply> ());
  }

  void submit (std::function<void (result_t<TReply>)> callback)
  {
    auto task = submit ();
    task.on_completed (std::move (callback));
  }

private:
  friend class connector_t;

  class erased_result_t
  {
  public:
    explicit erased_result_t (result_t<zlink::message_t> result)
      : _result (std::move (result))
    {
    }

    template<typename T>
    result_t<T> as () const
    {
      if (!_result) {
        return result_t<T>::failure (
          _result.error_code (),
          _result.error () ? _result.error ()->message : "request failed");
      }
      if constexpr (std::is_same_v<T, zlink::message_t>) {
        return result_t<T>::success (_result.value ());
      } else {
        T value {};
        detail::apply_packet_payload (value, _result.value (), 0);
        return result_t<T>::success (std::move (value));
      }
    }

  private:
    result_t<zlink::message_t> _result;
  };

  request_call_t (std::shared_ptr<detail::connector_state_t> state,
                  packet_t packet,
                  std::chrono::milliseconds default_timeout)
    : _state (std::move (state)),
      _packet (std::move (packet)),
      _timeout (default_timeout)
  {
  }

  erased_result_t submit_erased ()
  {
    return erased_result_t (
      detail::submit_request (_state, std::move (_packet), _timeout));
  }

  std::shared_ptr<detail::connector_state_t> _state;
  packet_t _packet;
  std::chrono::milliseconds _timeout { 0 };
};

} // namespace zlink::stream_connector
