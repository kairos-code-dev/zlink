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

    task_t<TReply> async () { return start (false); }

    task_t<TReply> yield () { return start (true); }

  private:
    task_t<TReply> start (bool release_turn)
    {
        if (_immediate) {
            return task_t<TReply> (*_immediate);
        }
        if (!_submit) {
            return task_t<TReply> (
              result_t<TReply>::failure (framework_error_kind_t::request_protocol_error,
                                         "request call is not bound to a channel client"));
        }
        auto turn_plan = detail::prepare_serial_turn_await (release_turn);
        if (!turn_plan) {
            return _submit (_packet_name, _timeout, _metadata);
        }
        // The transport submit may block its calling thread. It therefore runs
        // outside the serial executor even when async keeps the logical turn.
        auto source =
          std::make_shared<detail::task_completion_source_t<TReply>> (
            std::move (turn_plan->scheduler));
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
        return pending;
    }

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
        co_return co_await start<TReply> (false);
    }

    template <typename TReply> task_t<TReply> yield ()
    {
        co_return co_await start<TReply> (true);
    }

  protected:
    template <typename TReply> task_t<TReply> start (bool release_turn)
    {
        if (!_submit) {
            co_return result_t<TReply>::failure (framework_error_kind_t::request_protocol_error,
                                                 "request call is not bound to a channel client");
        }
        zlink::message_t reply;
        auto turn_plan = detail::prepare_serial_turn_await (release_turn);
        if (!turn_plan) {
            reply = co_await _submit (_packet_name, _timeout, _metadata);
        } else {
            auto source = std::make_shared<detail::task_completion_source_t<zlink::message_t>> (
              std::move (turn_plan->scheduler));
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
            reply = co_await pending;
        }
        co_return decode<TReply> (reply);
    }

    template <typename TReply> result_t<TReply> decode (const zlink::message_t &reply)
    {
        if (_serializers == nullptr) {
            return result_t<TReply>::failure (framework_error_kind_t::request_protocol_error,
                                              "channel request has no serializer registry");
        }
        try {
            return result_t<TReply>::success (_serializers->get<TReply> ().deserialize (
              detail::encoded_payload_from_raw (reply)));
        }
        catch (const framework_exception_t &error) {
            return detail::result_access_t::failure<TReply> (error);
        }
    }
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
