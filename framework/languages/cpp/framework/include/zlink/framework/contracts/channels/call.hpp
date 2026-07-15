/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework/contracts/detail/call_facade.hpp>
#include <zlink/framework/contracts/codecs/serializer.hpp>

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
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

inline void submit_one_way_task (task_t<void> task)
{
    auto observed = std::make_shared<task_t<void>> (std::move (task));
    detail::observe_task_completion (*observed, [observed] (const result_t<void> &) {});
}
} // namespace detail

template <typename TReply> class request_call_t
{
  public:
    using metadata_map_t = std::map<std::string, std::string>;
    using submit_fn_t = std::function<task_t<TReply> (
      const std::string &, std::chrono::milliseconds, const metadata_map_t &)>;

    explicit request_call_t (result_t<TReply> result) : _immediate (std::move (result)) {}

    request_call_t (std::string packet_name, submit_fn_t submit) :
        _packet_name (std::move (packet_name)), _submit (std::move (submit))
    {
    }

    request_call_t &timeout (std::chrono::milliseconds timeout)
    {
        _timeout = timeout;
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
            return task_t<TReply> (
              result_t<TReply>::failure (framework_error_kind_t::request_protocol_error,
                                         "request call is not bound to a channel client"));
        }
        auto turn_handle = detail::capture_current_serial_turn ();
        if (!turn_handle || turn_handle->released () || !turn_handle->release ()) {
            return _submit (_packet_name, _timeout, _metadata);
        }
        // The underlying submit blocks the calling thread until the reply
        // arrives, so it must run OFF the released Spot serial turn — otherwise
        // the single serial-execution thread stays parked in the blocking call
        // and no other serial work (sibling timers, other actors) can progress
        // while this handler awaits. Offload to a detached thread and resume the
        // coroutine back on the serial queue when the reply lands.
        auto source = std::make_shared<detail::task_completion_source_t<TReply>> ();
        auto pending = source->task ();
        std::thread ([source, submit = _submit, packet_name = _packet_name, timeout = _timeout,
                      metadata = _metadata] () mutable {
            try {
                source->complete (submit (packet_name, timeout, metadata).result ());
            }
            catch (const framework_exception_t &error) {
                source->complete (detail::result_access_t::failure<TReply> (error));
            }
            catch (...) {
                source->complete (result_t<TReply>::failure (
                  framework_error_kind_t::request_failed, "awaited request failed"));
            }
        }).detach ();
        return detail::reschedule_task (std::move (pending), turn_handle->resume_scheduler ());
    }

  private:
    std::optional<result_t<TReply>> _immediate;
    std::string _packet_name;
    std::chrono::milliseconds _timeout{0};
    metadata_map_t _metadata;
    submit_fn_t _submit;
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
        _submit (std::move (submit))
    {
    }

    channel_request_call_t &timeout (std::chrono::milliseconds timeout)
    {
        _timeout = timeout;
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
            co_return result_t<TReply>::failure (framework_error_kind_t::request_protocol_error,
                                                 "request call is not bound to a channel client");
        }
        zlink::message_t reply;
        auto turn_handle = detail::capture_current_serial_turn ();
        if (!turn_handle || turn_handle->released () || !turn_handle->release ()) {
            reply = co_await _submit (_packet_name, _timeout, _metadata);
        } else {
            // The channel request blocks its calling thread until the reply
            // arrives. Run it on a detached thread instead of the released Spot
            // serial turn so the single serial-execution thread is free to run
            // other serial work (sibling timers, other actors) while this
            // handler awaits.
            auto source = std::make_shared<detail::task_completion_source_t<zlink::message_t>> ();
            auto pending = source->task ();
            std::thread ([source, submit = blocking_submit ()] () mutable {
                try {
                    source->complete (submit ());
                }
                catch (const framework_exception_t &error) {
                    source->complete (detail::result_access_t::failure<zlink::message_t> (error));
                }
                catch (...) {
                    source->complete (result_t<zlink::message_t>::failure (
                      framework_error_kind_t::request_failed, "awaited channel request failed"));
                }
            }).detach ();
            reply = co_await detail::reschedule_task (std::move (pending),
                                                      turn_handle->resume_scheduler ());
        }
        if (_serializers == nullptr) {
            co_return result_t<TReply>::failure (framework_error_kind_t::request_protocol_error,
                                                 "channel request has no serializer registry");
        }
        try {
            co_return _serializers->get<TReply> ().deserialize (
              detail::encoded_payload_from_raw (reply));
        }
        catch (const framework_exception_t &error) {
            co_return detail::result_access_t::failure<TReply> (error);
        }
    }

  protected:
    task_t<zlink::message_t> submit_raw ()
    {
        if (!_submit) {
            return task_t<zlink::message_t> (result_t<zlink::message_t>::failure (
              framework_error_kind_t::request_protocol_error,
              "request call is not bound to a channel client"));
        }
        return _submit (_packet_name, _timeout, _metadata);
    }

    // Copyable blocking submit closure that can run on a thread other than the
    // caller's Spot serial turn. Captures copies of the request parameters so it
    // stays valid even after the originating call object is gone.
    std::function<result_t<zlink::message_t> ()> blocking_submit () const
    {
        if (!_submit) {
            return [] {
                return result_t<zlink::message_t>::failure (
                  framework_error_kind_t::request_protocol_error,
                  "request call is not bound to a channel client");
            };
        }
        return [submit = _submit, packet_name = _packet_name, timeout = _timeout,
                metadata = _metadata] () { return submit (packet_name, timeout, metadata).result (); };
    }

    const std::string &packet_name_value () const noexcept { return _packet_name; }
    std::chrono::milliseconds timeout_value () const noexcept { return _timeout; }
    const metadata_map_t &metadata_values () const noexcept { return _metadata; }
    serializer_registry_t *serializers () const noexcept { return _serializers; }

  private:
    std::string _packet_name;
    std::chrono::milliseconds _timeout{0};
    metadata_map_t _metadata;
    serializer_registry_t *_serializers = nullptr;
    submit_fn_t _submit;
};


class send_call_t
{
  public:
    using metadata_map_t = std::map<std::string, std::string>;
    using submit_fn_t = std::function<result_t<void> (
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

    send_call_t &metadata (std::string key, std::string value)
    {
        _metadata[std::move (key)] = std::move (value);
        return *this;
    }

    void submit () { submit_now ().value (); }

  private:
    result_t<void> submit_now ()
    {
        if (_immediate) {
            return *_immediate;
        }
        if (!_submit) {
            return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                            "send call is not bound to a channel client");
        }
        return _submit (_packet_name, _timeout, _metadata);
    }

    std::optional<result_t<void>> _immediate;
    std::string _packet_name;
    std::chrono::milliseconds _timeout{0};
    metadata_map_t _metadata;
    submit_fn_t _submit;
};

class bound_session_send_call_t
{
  public:
    explicit bound_session_send_call_t (send_call_t call) : _call (std::move (call)) {}

    bound_session_send_call_t &timeout (std::chrono::milliseconds timeout)
    {
        _call.timeout (timeout);
        return *this;
    }

    bound_session_send_call_t &metadata (std::string key, std::string value)
    {
        _call.metadata (std::move (key), std::move (value));
        return *this;
    }

    void submit () { _call.submit (); }

  private:
    send_call_t _call;
};

class relay_call_t : private detail::call_facade_t<relay_call_t, void>
{
  private:
    using base_t = detail::call_facade_t<relay_call_t, void>;

  public:
    explicit relay_call_t (result_t<void> result) : base_t (std::move (result)) {}

    using base_t::timeout;

    void submit () { detail::submit_one_way_task (base_t::async ()); }
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
    stream_write_call_t &compress ();
    void submit ();

  private:
    using submit_fn_t =
      std::function<result_t<void> (const detail::stream_header_t &, const zlink::message_t &)>;

    friend class stream_t;
    friend class detail::stream_write_call_state_t;

    stream_write_call_t (detail::stream_header_t header,
                         zlink::message_t payload,
                         std::shared_ptr<const stream_compression_codec_t> compression_codec,
                         submit_fn_t submit);

    result_t<void> submit_now ();

    std::shared_ptr<detail::stream_write_call_state_t> _state;
};

class stream_send_call_t
{
  public:
    explicit stream_send_call_t (result_t<void> result);
    ~stream_send_call_t ();

    stream_send_call_t (stream_send_call_t &&) noexcept;
    stream_send_call_t &operator= (stream_send_call_t &&) noexcept;
    stream_send_call_t (const stream_send_call_t &) = delete;
    stream_send_call_t &operator= (const stream_send_call_t &) = delete;

    stream_send_call_t &metadata (std::string key, std::string value);
    stream_send_call_t &packet_name (std::string packet_name);
    stream_send_call_t &compress ();
    void submit ();

  private:
    using submit_fn_t =
      std::function<result_t<void> (const detail::stream_header_t &, const zlink::message_t &)>;

    friend class stream_t;

    stream_send_call_t (detail::stream_header_t header,
                        zlink::message_t payload,
                        std::shared_ptr<const stream_compression_codec_t> compression_codec,
                        submit_fn_t submit);

    std::shared_ptr<detail::stream_write_call_state_t> _state;
};

} // namespace zlink::framework
