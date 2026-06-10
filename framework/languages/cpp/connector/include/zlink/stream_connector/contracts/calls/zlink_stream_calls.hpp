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
result_t<zlink::message_t>
submit_request (std::shared_ptr<void> state, packet_t packet, std::chrono::milliseconds timeout);
result_t<packet_t> submit_wait (std::shared_ptr<void> state,
                                std::string packet_name,
                                std::function<bool (const packet_t &)> predicate,
                                std::chrono::milliseconds timeout);
} // namespace detail

class send_call_t
{
  public:
    /// Creates an unbound send call that fails with configuration_error when submitted.
    send_call_t ();
    ~send_call_t ();

    send_call_t (send_call_t &&) noexcept;
    send_call_t &operator= (send_call_t &&) noexcept;
    send_call_t (const send_call_t &) = default;
    send_call_t &operator= (const send_call_t &) = default;

    /// Overrides the packet name sent with this call.
    send_call_t &packet_name (std::string name);

    /// Adds or replaces one metadata value copied into the outbound packet.
    send_call_t &metadata (std::string key, std::string value);

    /// Replaces the outbound packet metadata.
    send_call_t &metadata (metadata_t metadata);

    /// Sets the outbound payload codec.
    send_call_t &codec (codec_t codec);

    /// Marks the outbound packet for compression when compression is available.
    send_call_t &compress ();

    /// Sends the packet and blocks until the send result is known.
    result_t<void> submit ();

    /// Sends the packet as an awaitable task.
    task_t<void> submit_async ();

    /// Sends the packet and invokes the callback with the completion result.
    void submit_async (std::function<void (result_t<void>)> callback);

  private:
    friend class connector_t;
    send_call_t (std::shared_ptr<void> state, packet_t packet);

    std::shared_ptr<void> _state;
    packet_t _packet;
};

template <typename TReply> class request_call_t
{
  public:
    /// Creates an unbound request call that fails with configuration_error when submitted.
    request_call_t () = default;

    /// Overrides the packet name sent with this request.
    request_call_t &packet_name (std::string name)
    {
        _packet.name = std::move (name);
        return *this;
    }

    /// Adds or replaces one metadata value copied into the outbound request packet.
    request_call_t &metadata (std::string key, std::string value)
    {
        _packet.metadata.with (std::move (key), std::move (value));
        return *this;
    }

    /// Replaces the outbound request metadata.
    request_call_t &metadata (metadata_t metadata)
    {
        _packet.metadata = std::move (metadata);
        return *this;
    }

    /// Sets the outbound request payload codec.
    request_call_t &codec (codec_t codec)
    {
        _packet.codec = codec;
        return *this;
    }

    /// Sets the request timeout used by submit and submit_async.
    request_call_t &timeout (std::chrono::milliseconds timeout)
    {
        _timeout = timeout;
        return *this;
    }

    /// Marks the outbound request packet for compression when compression is available.
    request_call_t &compress ()
    {
        _packet.compressed = true;
        return *this;
    }

    /// Sends the request, waits for the correlated reply, and decodes it as TReply.
    result_t<TReply> submit ()
    {
        if (!_state) {
            return result_t<TReply>::failure (error_code_t::configuration_error,
                                              "request call has no connector");
        }
        return submit_erased ().template as<TReply> ();
    }

    /// Sends the request and returns an awaitable correlated reply.
    task_t<TReply> submit_async () { return task_t<TReply> (submit ()); }

    /// Sends the request and invokes the callback with the decoded reply result.
    void submit_async (std::function<void (result_t<TReply>)> callback)
    {
        auto task = submit_async ();
        task.on_completed (std::move (callback));
    }

  private:
    friend class connector_t;

    class erased_result_t
    {
      public:
        explicit erased_result_t (result_t<zlink::message_t> result) : _result (std::move (result))
        {
        }

        template <typename T> result_t<T> as () const
        {
            if (!_result) {
                return result_t<T>::failure (_result.error_code (), _result.error ()
                                                                      ? _result.error ()->message
                                                                      : "request failed");
            }
            if constexpr (std::is_same_v<T, zlink::message_t>) {
                return result_t<T>::success (_result.value ());
            } else {
                T value{};
                detail::apply_packet_payload (value, _result.value (), 0);
                return result_t<T>::success (std::move (value));
            }
        }

      private:
        result_t<zlink::message_t> _result;
    };

    request_call_t (std::shared_ptr<void> state,
                    packet_t packet,
                    std::chrono::milliseconds default_timeout) :
        _state (std::move (state)), _packet (std::move (packet)), _timeout (default_timeout)
    {
    }

    erased_result_t submit_erased ()
    {
        return erased_result_t (detail::submit_request (_state, std::move (_packet), _timeout));
    }

    std::shared_ptr<void> _state;
    packet_t _packet;
    std::chrono::milliseconds _timeout{0};
};

template <typename TMessage> class wait_call_t
{
  public:
    /// Creates an unbound wait call that fails with configuration_error when submitted.
    wait_call_t () = default;

    /// Overrides the packet name used to match received packets.
    wait_call_t &packet_name (std::string name)
    {
        _packet_name = std::move (name);
        return *this;
    }

    /// Sets the wait timeout used by submit and submit_async.
    wait_call_t &timeout (std::chrono::milliseconds timeout)
    {
        _timeout = timeout;
        return *this;
    }

    /// Restricts the wait to a decoded message that satisfies the predicate.
    wait_call_t &where (std::function<bool (const TMessage &)> predicate)
    {
        _predicate = std::move (predicate);
        return *this;
    }

    /// Waits for a matching packet, consumes it, and decodes it as TMessage.
    result_t<TMessage> submit ()
    {
        if (!_state) {
            return result_t<TMessage>::failure (error_code_t::configuration_error,
                                                "wait call has no connector");
        }

        std::function<bool (const packet_t &)> packet_predicate;
        if (_predicate) {
            packet_predicate = [predicate = _predicate] (const packet_t &packet) {
                if constexpr (std::is_same_v<TMessage, packet_t>) {
                    return predicate (packet);
                } else {
                    TMessage message{};
                    detail::apply_packet_payload (message, packet.payload, 0);
                    return predicate (message);
                }
            };
        }

        auto packet =
          detail::submit_wait (_state, _packet_name, std::move (packet_predicate), _timeout);
        if (!packet) {
            return result_t<TMessage>::failure (
              packet.error_code (),
              packet.error () ? packet.error ()->message : "stream connector wait failed");
        }
        if constexpr (std::is_same_v<TMessage, packet_t>) {
            return result_t<TMessage>::success (std::move (packet.value ()));
        } else {
            TMessage message{};
            detail::apply_packet_payload (message, packet.value ().payload, 0);
            return result_t<TMessage>::success (std::move (message));
        }
    }

    /// Waits for a matching packet as an awaitable task.
    task_t<TMessage> submit_async () { return task_t<TMessage> (submit ()); }

    /// Waits for a matching packet and invokes the callback with the decoded result.
    void submit_async (std::function<void (result_t<TMessage>)> callback)
    {
        auto task = submit_async ();
        task.on_completed (std::move (callback));
    }

  private:
    friend class connector_t;

    wait_call_t (std::shared_ptr<void> state,
                 std::string packet_name,
                 std::chrono::milliseconds default_timeout) :
        _state (std::move (state)),
        _packet_name (std::move (packet_name)), _timeout (default_timeout)
    {
    }

    std::shared_ptr<void> _state;
    std::string _packet_name;
    std::chrono::milliseconds _timeout{0};
    std::function<bool (const TMessage &)> _predicate;
};

} // namespace zlink::stream_connector
