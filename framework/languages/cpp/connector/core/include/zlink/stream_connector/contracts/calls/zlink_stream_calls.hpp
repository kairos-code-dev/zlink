/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>
#include <zlink/stream_connector/contracts/result.hpp>
#include <zlink/stream_connector/contracts/stream_payload.hpp>
#include <zlink/stream_connector/contracts/zlink_stream_connector_options.hpp>
#include <zlink/stream_connector/contracts/zlink_stream_interfaces.hpp>
#include <zlink/stream_connector/contracts/zlink_stream_models.hpp>

#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace zlink::stream_connector
{

namespace detail
{
struct request_reply_t
{
    codec_t codec = codec_t::raw;
    zlink::message_t payload;
};

result_t<request_reply_t>
submit_request (std::shared_ptr<void> state, packet_t packet, std::chrono::milliseconds timeout);
void submit_request_async (std::shared_ptr<void> state,
                           packet_t packet,
                           std::chrono::milliseconds timeout,
                           std::function<void (result_t<request_reply_t>)> callback,
                           bool deliver_direct = false);
result_t<packet_t> submit_wait (std::shared_ptr<void> state,
                                std::string packet_name,
                                std::function<bool (const packet_t &)> predicate,
                                std::chrono::milliseconds timeout);
void submit_wait_async (std::shared_ptr<void> state,
                        std::string packet_name,
                        std::function<bool (const packet_t &)> predicate,
                        std::chrono::milliseconds timeout,
                        std::function<void (result_t<packet_t>)> callback);
void post_runtime_operation (std::function<void ()> operation);
void schedule_delivery (std::shared_ptr<void> state, std::function<void ()> callback);
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

    /// Gives the packet to the connector for delivery.
    void submit ();

  private:
    friend class connector_t;
    send_call_t (std::shared_ptr<void> state, packet_t packet);

    std::shared_ptr<void> _state;
    packet_t _packet;
};

class request_call_t
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

    /// Sets the request timeout used by submit.
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
    template <typename TReply> result_t<TReply> submit ()
    {
        if (!_state) {
            return result_t<TReply>::failure (error_code_t::configuration_error,
                                              "request call has no connector");
        }
        return submit_erased ().template as<TReply> ();
    }

    /// Sends the request and invokes the callback with the decoded reply result.
    template <typename TReply> void submit (std::function<void (result_t<TReply>)> callback)
    {
        if (!_state) {
            if (callback) {
                callback (result_t<TReply>::failure (error_code_t::configuration_error,
                                                     "request call has no connector"));
            }
            return;
        }
        auto state = _state;
        auto packet = std::move (_packet);
        const auto timeout = _timeout;
        detail::submit_request_async (
          state, std::move (packet), timeout,
          [callback = std::move (callback)] (result_t<detail::request_reply_t> reply) mutable {
              erased_result_t erased (std::move (reply));
              auto result = erased.template as<TReply> ();
              if (callback) {
                  callback (std::move (result));
              }
          });
    }

  private:
    friend class connector_t;

    class erased_result_t
    {
      public:
        explicit erased_result_t (result_t<detail::request_reply_t> result) :
            _result (std::move (result))
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
                return result_t<T>::success (_result.value ().payload);
            } else {
                T value{};
                try {
                    detail::apply_packet_payload (value, _result.value ().codec,
                                                  _result.value ().payload, 0);
                } catch (const std::exception &ex) {
                    return result_t<T>::failure (error_code_t::frame_decode_failed, ex.what ());
                }
                return result_t<T>::success (std::move (value));
            }
        }

      private:
        result_t<detail::request_reply_t> _result;
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

    /// Sets the wait timeout used by submit.
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

    template <typename TValue, typename TExpected>
    wait_call_t &where (TValue TMessage::*member, TExpected &&expected)
    {
        auto expected_value = std::decay_t<TExpected> (std::forward<TExpected> (expected));
        return where (
          [member, expected_value = std::move (expected_value)] (const TMessage &message) {
              return std::invoke (member, message) == expected_value;
          });
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
                    detail::apply_packet_payload (message, packet.codec, packet.payload, 0);
                    return predicate (message);
                }
            };
        }

        auto packet =
          detail::submit_wait (_state, _packet_name, std::move (packet_predicate), _timeout);
        if (!packet) {
            return result_t<TMessage>::failure (packet.error_code (),
                                                packet.error () ? packet.error ()->message
                                                                : "stream connector wait failed");
        }
        if constexpr (std::is_same_v<TMessage, packet_t>) {
            return result_t<TMessage>::success (std::move (packet.value ()));
        } else {
            TMessage message{};
            detail::apply_packet_payload (message, packet.value ().codec, packet.value ().payload,
                                          0);
            return result_t<TMessage>::success (std::move (message));
        }
    }

    /// Waits for a matching packet and invokes the callback with the decoded result.
    void submit (std::function<void (result_t<TMessage>)> callback)
    {
        if (!_state) {
            if (callback) {
                callback (result_t<TMessage>::failure (error_code_t::configuration_error,
                                                       "wait call has no connector"));
            }
            return;
        }

        std::function<bool (const packet_t &)> packet_predicate;
        if (_predicate) {
            packet_predicate = [predicate = _predicate] (const packet_t &packet) {
                if constexpr (std::is_same_v<TMessage, packet_t>) {
                    return predicate (packet);
                } else {
                    TMessage message{};
                    detail::apply_packet_payload (message, packet.codec, packet.payload, 0);
                    return predicate (message);
                }
            };
        }

        auto state = _state;
        auto packet_name = std::move (_packet_name);
        const auto timeout = _timeout;
        detail::submit_wait_async (
          state, std::move (packet_name), std::move (packet_predicate), timeout,
          [callback = std::move (callback)] (result_t<packet_t> packet) mutable {
              result_t<TMessage> result = result_t<TMessage>::failure (
                error_code_t::configuration_error, "stream connector wait failed");
              if (!packet) {
                  result = result_t<TMessage>::failure (
                    packet.error_code (),
                    packet.error () ? packet.error ()->message : "stream connector wait failed");
              } else if constexpr (std::is_same_v<TMessage, packet_t>) {
                  result = result_t<TMessage>::success (std::move (packet.value ()));
              } else {
                  TMessage message{};
                  detail::apply_packet_payload (message, packet.value ().codec,
                                                packet.value ().payload, 0);
                  result = result_t<TMessage>::success (std::move (message));
              }
              if (callback) {
                  callback (std::move (result));
              }
          });
    }

    std::future<TMessage> to_future (std::string failure_message = "stream connector wait failed")
    {
        auto promise = std::make_shared<std::promise<TMessage>> ();
        auto future = promise->get_future ();
        submit ([promise, failure_message = std::move (failure_message)] (
                  result_t<TMessage> result) mutable {
            try {
                if (!result) {
                    promise->set_exception (
                      std::make_exception_ptr (std::runtime_error (failure_message)));
                    return;
                }
                promise->set_value (std::move (result.value ()));
            }
            catch (...) {
                promise->set_exception (std::current_exception ());
            }
        });
        return future;
    }

  private:
    friend class connector_t;

    wait_call_t (std::shared_ptr<void> state,
                 std::string packet_name,
                 std::chrono::milliseconds default_timeout) :
        _state (std::move (state)),
        _packet_name (std::move (packet_name)),
        _timeout (default_timeout)
    {
    }

    std::shared_ptr<void> _state;
    std::string _packet_name;
    std::chrono::milliseconds _timeout{0};
    std::function<bool (const TMessage &)> _predicate;
};

} // namespace zlink::stream_connector
