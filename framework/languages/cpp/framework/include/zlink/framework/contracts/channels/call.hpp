/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/detail/call_facade.hpp>
#include <zlink/framework/contracts/codecs/serializer.hpp>

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace zlink
{
class message_t;
} // namespace zlink

namespace zlink::framework
{

class stream_compression_codec_t;

namespace detail
{
class stream_write_call_state_t;
class stream_header_t;
} // namespace detail

template <typename TReply> class request_call_t
{
  public:
    using metadata_map_t = std::map<std::string, std::string>;
    using submit_fn_t = std::function<task_t<TReply> (
      const std::string &, std::chrono::milliseconds, const metadata_map_t &)>;

    explicit request_call_t (result_t<TReply> result) : _immediate (std::move (result)) {}

    request_call_t (std::string packet_name, submit_fn_t submit) :
        _packet_name (std::move (packet_name)),
        _submit (std::move (submit)),
        _yield_turn (detail::capture_current_serial_yield_turn ())
    {
    }

    request_call_t &timeout (std::chrono::milliseconds timeout)
    {
        _timeout = timeout;
        return *this;
    }

    request_call_t &packet_name (std::string packet_name)
    {
        _packet_name = std::move (packet_name);
        return *this;
    }

    request_call_t &metadata (std::string key, std::string value)
    {
        _metadata[std::move (key)] = std::move (value);
        return *this;
    }

    task_t<TReply> async ()
    {
        if (_immediate) {
            return task_t<TReply> (*_immediate);
        }
        if (!_submit) {
            return task_t<TReply> (result_t<TReply>::failure (
              framework_error_kind_t::request_protocol_error,
              "request call is not bound to a channel client"));
        }
        return _submit (_packet_name, _timeout, _metadata);
    }

    task_t<TReply> yield_async ()
    {
        if (_immediate) {
            return task_t<TReply> (*_immediate);
        }
        if (!_submit) {
            return task_t<TReply> (result_t<TReply>::failure (
              framework_error_kind_t::request_protocol_error,
              "request call is not bound to a channel client"));
        }
        if (!_yield_turn) {
            return task_t<TReply> (result_t<TReply>::failure (
              framework_error_kind_t::request_protocol_error,
              "yield_async requires a framework Spot handler turn captured when the call object was created"));
        }
        if (!_yield_turn->release ()) {
            return task_t<TReply> (result_t<TReply>::failure (
              framework_error_kind_t::request_protocol_error,
              "yield_async could not release the current Spot handler turn"));
        }
        return detail::reschedule_task (_submit (_packet_name, _timeout, _metadata),
                                        _yield_turn->resume_scheduler ());
    }

  private:
    std::optional<result_t<TReply>> _immediate;
    std::string _packet_name;
    std::chrono::milliseconds _timeout{0};
    metadata_map_t _metadata;
    submit_fn_t _submit;
    std::shared_ptr<detail::serial_yield_turn_t> _yield_turn;
};

class channel_request_call_t
{
  public:
    using metadata_map_t = std::map<std::string, std::string>;
    using submit_fn_t = std::function<task_t<zlink::message_t> (
      const std::string &, std::chrono::milliseconds, const metadata_map_t &)>;

    channel_request_call_t () = default;

    channel_request_call_t (std::string packet_name,
                            serializer_registry_t *serializers,
                            submit_fn_t submit) :
        _packet_name (std::move (packet_name)),
        _serializers (serializers),
        _submit (std::move (submit)),
        _yield_turn (detail::capture_current_serial_yield_turn ())
    {
    }

    channel_request_call_t &timeout (std::chrono::milliseconds timeout)
    {
        _timeout = timeout;
        return *this;
    }

    channel_request_call_t &packet_name (std::string packet_name)
    {
        _packet_name = std::move (packet_name);
        return *this;
    }

    channel_request_call_t &metadata (std::string key, std::string value)
    {
        _metadata[std::move (key)] = std::move (value);
        return *this;
    }

    template <typename TReply> task_t<TReply> async ()
    {
        if (!_submit) {
            co_return result_t<TReply>::failure (
              framework_error_kind_t::request_protocol_error,
              "request call is not bound to a channel client");
        }
        auto reply = co_await _submit (_packet_name, _timeout, _metadata);
        if (_serializers == nullptr) {
            co_return result_t<TReply>::failure (
              framework_error_kind_t::request_protocol_error,
              "channel request has no serializer registry");
        }
        try {
            co_return _serializers->get<TReply> ().deserialize (
              detail::encoded_payload_from_raw (reply));
        }
        catch (const framework_exception_t &error) {
            co_return result_t<TReply>::failure (
              error.kind (), error.what (), error.is_retriable ());
        }
    }

    template <typename TReply> task_t<TReply> yield_async ()
    {
        if (!_submit) {
            co_return result_t<TReply>::failure (
              framework_error_kind_t::request_protocol_error,
              "request call is not bound to a channel client");
        }
        if (!_yield_turn) {
            co_return result_t<TReply>::failure (
              framework_error_kind_t::request_protocol_error,
              "yield_async requires a framework Spot handler turn captured when the call object was created");
        }
        if (!_yield_turn->release ()) {
            co_return result_t<TReply>::failure (
              framework_error_kind_t::request_protocol_error,
              "yield_async could not release the current Spot handler turn");
        }
        auto reply = co_await detail::reschedule_task (
          _submit (_packet_name, _timeout, _metadata), _yield_turn->resume_scheduler ());
        if (_serializers == nullptr) {
            co_return result_t<TReply>::failure (
              framework_error_kind_t::request_protocol_error,
              "channel request has no serializer registry");
        }
        try {
            co_return _serializers->get<TReply> ().deserialize (
              detail::encoded_payload_from_raw (reply));
        }
        catch (const framework_exception_t &error) {
            co_return result_t<TReply>::failure (
              error.kind (), error.what (), error.is_retriable ());
        }
    }

  private:
    std::string _packet_name;
    std::chrono::milliseconds _timeout{0};
    metadata_map_t _metadata;
    serializer_registry_t *_serializers = nullptr;
    submit_fn_t _submit;
    std::shared_ptr<detail::serial_yield_turn_t> _yield_turn;
};

class send_call_t
{
  public:
    using metadata_map_t = std::map<std::string, std::string>;
    using submit_fn_t = std::function<task_t<void> (
      const std::string &, std::chrono::milliseconds, const metadata_map_t &)>;

    explicit send_call_t (result_t<void> result) : _immediate (std::move (result)) {}

    send_call_t (std::string packet_name, submit_fn_t submit) :
        _packet_name (std::move (packet_name)), _submit (std::move (submit))
    {
    }

    send_call_t &timeout (std::chrono::milliseconds timeout)
    {
        _timeout = timeout;
        return *this;
    }

    send_call_t &packet_name (std::string packet_name)
    {
        _packet_name = std::move (packet_name);
        return *this;
    }

    send_call_t &metadata (std::string key, std::string value)
    {
        _metadata[std::move (key)] = std::move (value);
        return *this;
    }

    task_t<void> async ()
    {
        if (_immediate) {
            return task_t<void> (*_immediate);
        }
        if (!_submit) {
            return task_t<void> (result_t<void>::failure (
              framework_error_kind_t::request_protocol_error,
              "send call is not bound to a channel client"));
        }
        return _submit (_packet_name, _timeout, _metadata);
    }

  private:
    std::optional<result_t<void>> _immediate;
    std::string _packet_name;
    std::chrono::milliseconds _timeout{0};
    metadata_map_t _metadata;
    submit_fn_t _submit;
};

class bound_session_send_call_t
{
  public:
    explicit bound_session_send_call_t (send_call_t call) :
        _call (std::move (call)),
        _yield_turn (detail::capture_current_serial_yield_turn ())
    {
    }

    bound_session_send_call_t &timeout (std::chrono::milliseconds timeout)
    {
        _call.timeout (timeout);
        return *this;
    }

    bound_session_send_call_t &packet_name (std::string packet_name)
    {
        _call.packet_name (std::move (packet_name));
        return *this;
    }

    bound_session_send_call_t &metadata (std::string key, std::string value)
    {
        _call.metadata (std::move (key), std::move (value));
        return *this;
    }

    task_t<void> async () { return _call.async (); }

    task_t<void> yield_async ()
    {
        if (!_yield_turn) {
            return task_t<void> (result_t<void>::failure (
              framework_error_kind_t::request_protocol_error,
              "yield_async requires a framework Spot handler turn captured when the call object was created"));
        }
        if (!_yield_turn->release ()) {
            return task_t<void> (result_t<void>::failure (
              framework_error_kind_t::request_protocol_error,
              "yield_async could not release the current Spot handler turn"));
        }
        return detail::reschedule_task (_call.async (), _yield_turn->resume_scheduler ());
    }

  private:
    send_call_t _call;
    std::shared_ptr<detail::serial_yield_turn_t> _yield_turn;
};

class relay_call_t : private detail::call_facade_t<relay_call_t, void>
{
  private:
    using base_t = detail::call_facade_t<relay_call_t, void>;

  public:
    explicit relay_call_t (result_t<void> result) : base_t (std::move (result)) {}

    using base_t::async;
    using base_t::timeout;
};

class stream_write_call_t
{
  public:
    using metadata_map_t = std::map<std::string, std::string>;

    explicit stream_write_call_t (result_t<void> result);
    ~stream_write_call_t ();

    stream_write_call_t (stream_write_call_t &&) noexcept;
    stream_write_call_t &operator= (stream_write_call_t &&) noexcept;
    stream_write_call_t (const stream_write_call_t &) = delete;
    stream_write_call_t &operator= (const stream_write_call_t &) = delete;

    stream_write_call_t &metadata (std::string key, std::string value);
    stream_write_call_t &packet_name (std::string packet_name);
    stream_write_call_t &compress ();
    task_t<void> async ();

  private:
    using submit_fn_t =
      std::function<task_t<void> (const detail::stream_header_t &, const zlink::message_t &)>;

    friend class stream_t;
    friend class detail::stream_write_call_state_t;

    stream_write_call_t (detail::stream_header_t header,
                         zlink::message_t payload,
                         std::shared_ptr<const stream_compression_codec_t> compression_codec,
                         submit_fn_t submit);

    std::shared_ptr<detail::stream_write_call_state_t> _state;
};

} // namespace zlink::framework
