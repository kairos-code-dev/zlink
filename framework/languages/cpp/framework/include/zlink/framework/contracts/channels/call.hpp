/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework/contracts/detail/call_facade.hpp>
#include <zlink/framework/contracts/codecs/serializer.hpp>

#include <chrono>
#include <atomic>
#include <cstdint>
#include <exception>
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

class submit_once_t
{
  public:
    bool try_claim () noexcept
    {
        return !_claimed.exchange (true, std::memory_order_acq_rel);
    }

  private:
    std::atomic_bool _claimed{false};
};
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


enum class submit_status_t
{
    submitted,
    backpressured,
    timed_out,
    target_not_found,
    route_not_connected,
    shutdown
};

struct submit_result_t
{
    submit_status_t status = submit_status_t::submitted;
};

struct publish_result_t;

namespace detail
{
inline result_t<submit_result_t> one_way_submit_result (const result_t<void> &result)
{
    if (result) {
        return result_t<submit_result_t>::success ({submit_status_t::submitted});
    }

    const auto *error = result.error ();
    if (error == nullptr) {
        return result_t<submit_result_t>::failure (
          framework_error_kind_t::request_failed, "one-way submit failed");
    }
    switch (boundary_state (*error)) {
        case boundary_error_t::timed_out:
            return result_t<submit_result_t>::success ({submit_status_t::timed_out});
        case boundary_error_t::shutdown:
        case boundary_error_t::closed:
        case boundary_error_t::cancelled:
            return result_t<submit_result_t>::success ({submit_status_t::shutdown});
        case boundary_error_t::disconnected:
            return result_t<submit_result_t>::success ({submit_status_t::route_not_connected});
        case boundary_error_t::none:
        case boundary_error_t::stale_generation:
            break;
    }
    switch (error->kind ()) {
        case framework_error_kind_t::worker_queue_full:
            return result_t<submit_result_t>::success ({submit_status_t::backpressured});
        case framework_error_kind_t::route_not_connected:
            return result_t<submit_result_t>::success ({submit_status_t::route_not_connected});
        case framework_error_kind_t::actor_route_not_found:
        case framework_error_kind_t::spot_route_not_found:
        case framework_error_kind_t::request_target_not_found:
            return result_t<submit_result_t>::success ({submit_status_t::target_not_found});
        default:
            return result_access_t::failure<submit_result_t> (*error);
    }
}

task_t<submit_result_t>
submit_one_way_task (std::function<result_t<void> ()> submit);

task_t<publish_result_t>
submit_logical_multicast_task (std::function<publish_result_t ()> submit);

inline task_t<submit_result_t> map_one_way_task (task_t<void> task)
{
    auto source = std::make_shared<task_completion_source_t<submit_result_t>> ();
    auto output = source->task ();
    auto observed = std::make_shared<task_t<void>> (std::move (task));
    detail::observe_task_completion (
      *observed, [source, observed] (const result_t<void> &result) {
          source->complete (one_way_submit_result (result));
      });
    return output;
}
} // namespace detail

class send_call_t
{
  public:
    using metadata_map_t = std::map<std::string, std::string>;
    using submit_fn_t =
      std::function<result_t<void> (const std::string &, const metadata_map_t &)>;

    explicit send_call_t (result_t<void> result) : _immediate (std::move (result)) {}

    send_call_t (std::string packet_name, submit_fn_t submit) :
        _packet_name (std::move (packet_name)), _submit (std::move (submit))
    {
    }

    send_call_t &metadata (std::string key, std::string value)
    {
        _metadata[std::move (key)] = std::move (value);
        return *this;
    }

    task_t<submit_result_t> submit ()
    {
        if (!_submission->try_claim ()) {
            return task_t<submit_result_t> (result_t<submit_result_t>::failure (
              framework_error_kind_t::request_protocol_error,
              "one-way call has already been submitted"));
        }
        if (_immediate) {
            return task_t<submit_result_t> (detail::one_way_submit_result (*_immediate));
        }
        if (!_submit) {
            return task_t<submit_result_t> (result_t<submit_result_t>::failure (
              framework_error_kind_t::request_protocol_error,
              "send call is not bound to a channel client"));
        }
        return detail::submit_one_way_task (
          [packet_name = _packet_name, metadata = _metadata,
           submit = _submit] () { return submit (packet_name, metadata); });
    }

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
        return _submit (_packet_name, _metadata);
    }

    std::optional<result_t<void>> _immediate;
    std::string _packet_name;
    metadata_map_t _metadata;
    submit_fn_t _submit;
    std::shared_ptr<detail::submit_once_t> _submission =
      std::make_shared<detail::submit_once_t> ();
};

struct logical_multicast_detail_t
{
    // Counts the immutable remote target snapshot used for this publish.
    std::uint64_t snapshot_remote_node_count = 0;
    // Counts remote targets whose source-local outbound transport queue
    // accepted the publish. It does not wait for a remote Spot queue or handler.
    std::uint64_t admitted_remote_node_count = 0;
    std::uint64_t dropped_remote_node_count = 0;
    std::uint64_t unreachable_remote_node_count = 0;
    // Counts the immutable local Spot snapshot used for this publish.
    std::uint64_t snapshot_local_spot_count = 0;
    // Counts local Spots whose serial queue accepted the publish.
    // It does not wait for handler execution.
    std::uint64_t admitted_local_spot_count = 0;
    std::uint64_t dropped_local_spot_count = 0;
};

struct publish_result_t
{
    // Reports submission through local transport and Spot queue admission only.
    // Completion never means that a remote queue or application handler ran.
    submit_status_t status = submit_status_t::submitted;
    logical_multicast_detail_t detail;
};

class publish_call_t
{
  public:
    using metadata_map_t = std::map<std::string, std::string>;
    using submit_fn_t = std::function<publish_result_t (const metadata_map_t &)>;

    explicit publish_call_t (publish_result_t result) :
        _immediate (std::move (result))
    {
    }

    explicit publish_call_t (submit_fn_t submit) : _submit (std::move (submit)) {}

    publish_call_t &metadata (std::string key, std::string value)
    {
        _metadata[std::move (key)] = std::move (value);
        return *this;
    }

    task_t<publish_result_t> submit ()
    {
        if (!_submission->try_claim ()) {
            return task_t<publish_result_t> (result_t<publish_result_t>::failure (
              framework_error_kind_t::request_protocol_error,
              "logical multicast call has already been submitted"));
        }
        if (_immediate) {
            return task_t<publish_result_t> (
              result_t<publish_result_t>::success (*_immediate));
        }
        return detail::submit_logical_multicast_task (
          [submit = _submit, metadata = _metadata] () mutable {
              if (!submit) {
                  throw framework_exception_t (
                    framework_error_kind_t::request_protocol_error,
                    "logical multicast call is not bound to a publisher");
              }
              return submit (metadata);
          });
    }

  private:
    std::optional<publish_result_t> _immediate;
    metadata_map_t _metadata;
    submit_fn_t _submit;
    std::shared_ptr<detail::submit_once_t> _submission =
      std::make_shared<detail::submit_once_t> ();
};

class bound_session_send_call_t
{
  public:
    explicit bound_session_send_call_t (send_call_t call) : _call (std::move (call)) {}

    bound_session_send_call_t &metadata (std::string key, std::string value)
    {
        _call.metadata (std::move (key), std::move (value));
        return *this;
    }

    task_t<submit_result_t> submit () { return _call.submit (); }

  private:
    send_call_t _call;
};

class fanout_publish_call_t
{
  public:
    explicit fanout_publish_call_t (send_call_t call) : _call (std::move (call)) {}

    task_t<submit_result_t> submit () { return _call.submit (); }

  private:
    send_call_t _call;
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
    task_t<submit_result_t> submit ();

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
    task_t<submit_result_t> submit ();

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
