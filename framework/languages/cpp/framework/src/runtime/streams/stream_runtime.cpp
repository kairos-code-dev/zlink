/* SPDX-License-Identifier: MPL-2.0 */

#include "stream_runtime.hpp"

#include <zlink/framework/contracts/configuration/zlink_builder.hpp>

#include "runtime/channels/channel_runtime.hpp"
#include "runtime/dispatch/coroutine_executor.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace zlink::framework
{

namespace detail
{

class stream_write_call_state_t
{
  public:
    explicit stream_write_call_state_t (result_t<void> result) : _immediate (std::move (result)) {}

    stream_write_call_state_t (stream_header_t header,
                               zlink::message_t payload,
                               stream_write_call_t::submit_fn_t submit) :
        _header (std::move (header)), _payload (std::move (payload)), _submit (std::move (submit))
    {
    }

    void timeout (std::chrono::milliseconds timeout) { _timeout = timeout; }

    void metadata (std::string key, std::string value)
    {
        _metadata[std::move (key)] = std::move (value);
    }

    void packet_name (std::string packet_name) { _packet_name = std::move (packet_name); }

    void compress () { _compressed = true; }

    result_t<void> submit ()
    {
        if (_immediate) {
            return *_immediate;
        }
        if (!_submit || !_header || !_payload) {
            return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                            "STREAM write call is not bound to a stream");
        }

        auto metadata = _header->metadata ().values ();
        for (const auto &[key, value] : _metadata) {
            metadata[key] = value;
        }
        auto flags = _header->flags ();
        if (_compressed) {
            flags = flags | stream_header_flags_t::payload_compressed;
        }
        const auto packet_name =
          _packet_name.empty () ? std::string (_header->packet_name ()) : _packet_name;
        const auto header =
          stream_header_t (_header->kind (), _header->codec (), flags, _header->request_seq (),
                           packet_name, stream_metadata_t (std::move (metadata)));
        return _submit (header, *_payload);
    }

  private:
    std::optional<result_t<void>> _immediate;
    std::optional<stream_header_t> _header;
    std::optional<zlink::message_t> _payload;
    stream_write_call_t::submit_fn_t _submit;
    std::chrono::milliseconds _timeout{0};
    stream_write_call_t::metadata_map_t _metadata;
    std::string _packet_name;
    bool _compressed = false;
};

class stream_session_dispatcher_t
{
  public:
    explicit stream_session_dispatcher_t (stream_state_t &stream) : _stream (stream) {}

    result_t<void> dispatch (std::string operation, std::function<task_t<void> ()> callback) const
    {
        record_operation (std::move (operation));
        return runtime::handler_coroutine_executor ()
          .submit<void> ([callback = std::move (
                            callback)] () mutable -> boost::asio::awaitable<result_t<void>> {
              try {
                  auto callback_task = callback ();
                  (co_await runtime::await_task_result (std::move (callback_task))).value ();
                  co_return result_t<void>::success ();
              }
              catch (const framework_exception_t &error) {
                  co_return result_t<void>::failure (error.kind (), error.what (),
                                                     error.is_retriable ());
              }
              catch (...) {
                  co_return result_t<void>::failure (framework_error_kind_t::request_failed,
                                                     "stream session callback threw an exception");
              }
          })
          .result ();
    }

  private:
    void record_operation (std::string operation) const
    {
        _stream.serial_log.push_back (std::move (operation));
    }

    stream_state_t &_stream;
};

} // namespace detail

stream_write_call_t::stream_write_call_t (result_t<void> result) :
    _state (std::make_shared<detail::stream_write_call_state_t> (std::move (result)))
{
}

stream_write_call_t::stream_write_call_t (stream_header_t header,
                                          zlink::message_t payload,
                                          submit_fn_t submit) :
    _state (std::make_shared<detail::stream_write_call_state_t> (
      std::move (header), std::move (payload), std::move (submit)))
{
}

stream_write_call_t::~stream_write_call_t () = default;
stream_write_call_t::stream_write_call_t (stream_write_call_t &&) noexcept = default;
stream_write_call_t &stream_write_call_t::operator= (stream_write_call_t &&) noexcept = default;

stream_write_call_t &stream_write_call_t::timeout (std::chrono::milliseconds timeout)
{
    _state->timeout (timeout);
    return *this;
}

stream_write_call_t &stream_write_call_t::metadata (std::string key, std::string value)
{
    _state->metadata (std::move (key), std::move (value));
    return *this;
}

stream_write_call_t &stream_write_call_t::packet_name (std::string packet_name)
{
    _state->packet_name (std::move (packet_name));
    return *this;
}

stream_write_call_t &stream_write_call_t::compress ()
{
    _state->compress ();
    return *this;
}

result_t<void> stream_write_call_t::submit ()
{
    return _state->submit ();
}

task_t<void> stream_write_call_t::submit_async ()
{
    return task_t<void> (submit ());
}

pending_operation_t stream_write_call_t::submit_async (
  std::function<void (result_t<void>)> callback)
{
    callback (submit ());
    return pending_operation_t::make_completed ();
}

stream_error_t::stream_error_t (stream_session_error_t error,
                                int native_code,
                                std::string message) :
    _error (error), _native_code (native_code), _message (std::move (message))
{
}

stream_session_error_t stream_error_t::error () const noexcept
{
    return _error;
}

int stream_error_t::native_code () const noexcept
{
    return _native_code;
}

std::string_view stream_error_t::message () const noexcept
{
    return _message;
}

stream_metadata_t::stream_metadata_t (std::map<std::string, std::string> values) :
    _values (std::move (values))
{
}

stream_metadata_t &stream_metadata_t::with (std::string key, std::string value)
{
    _values[std::move (key)] = std::move (value);
    return *this;
}

std::optional<std::string_view> stream_metadata_t::find (std::string_view key) const
{
    const auto found = _values.find (std::string (key));
    if (found == _values.end ()) {
        return std::nullopt;
    }
    return std::string_view (found->second);
}

bool stream_metadata_t::empty () const noexcept
{
    return _values.empty ();
}

const std::map<std::string, std::string> &stream_metadata_t::values () const noexcept
{
    return _values;
}

stream_header_t::stream_header_t () = default;

stream_header_t::stream_header_t (stream_message_kind_t kind,
                                  stream_codec_t codec,
                                  stream_header_flags_t flags,
                                  std::optional<std::uint64_t> request_seq,
                                  std::string packet_name,
                                  stream_metadata_t metadata) :
    _kind (kind),
    _codec (codec),
    _flags (flags),
    _request_seq (request_seq),
    _packet_name (std::move (packet_name)),
    _metadata (std::move (metadata))
{
}

stream_message_kind_t stream_header_t::kind () const noexcept
{
    return _kind;
}

stream_codec_t stream_header_t::codec () const noexcept
{
    return _codec;
}

stream_header_flags_t stream_header_t::flags () const noexcept
{
    return _flags;
}

std::optional<std::uint64_t> stream_header_t::request_seq () const noexcept
{
    return _request_seq;
}

std::string_view stream_header_t::packet_name () const noexcept
{
    return _packet_name;
}

std::optional<std::string_view> stream_header_t::metadata (std::string_view key) const
{
    return _metadata.find (key);
}

const stream_metadata_t &stream_header_t::metadata () const noexcept
{
    return _metadata;
}

std::optional<std::string_view> stream_header_t::correlation_id () const
{
    return metadata ("correlation_id");
}

std::optional<std::string_view> stream_header_t::content_type () const
{
    return metadata ("content_type");
}

stream_t::stream_t () : _state (std::make_shared<detail::stream_state_t> ())
{
}

stream_t::stream_t (std::shared_ptr<detail::stream_state_t> state) : _state (std::move (state))
{
}

stream_t::~stream_t () = default;
stream_t::stream_t (stream_t &&) noexcept = default;
stream_t &stream_t::operator= (stream_t &&) noexcept = default;

std::string stream_t::session_id () const
{
    return _state->session_id;
}

task_t<void> stream_t::close ()
{
    _state->closed = true;
    return task_t<void> (result_t<void>::success ());
}

stream_write_call_t stream_t::write_packet (const stream_header_t &header,
                                            const zlink::message_t &payload)
{
    auto state = _state;
    return stream_write_call_t (
      header, payload,
      [state] (const stream_header_t &submitted_header, const zlink::message_t &submitted_payload) {
          if (state->closed) {
              return result_t<void>::failure (framework_error_kind_t::disconnected,
                                              "STREAM session is disconnected");
          }
          state->written_headers.push_back (submitted_header);
          state->written_payloads.push_back (submitted_payload);
          return result_t<void>::success ();
      });
}

stream_write_call_t stream_t::reply_packet (const stream_header_t &request_header,
                                            const zlink::message_t &payload)
{
    if (!request_header.request_seq ()) {
        return stream_write_call_t (
          result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                   "STREAM reply requires request sequence"));
    }
    stream_header_t reply_header (stream_message_kind_t::response, request_header.codec (),
                                  stream_header_flags_t::has_request_seq,
                                  request_header.request_seq (),
                                  std::string (request_header.packet_name ()), {});
    return write_packet (reply_header, payload);
}

stream_builder_t::stream_builder_t () :
    _state (std::make_shared<detail::stream_builder_state_t> (""))
{
}

stream_builder_t::stream_builder_t (std::shared_ptr<detail::stream_builder_state_t> state) :
    _state (std::move (state))
{
}

stream_builder_t::~stream_builder_t () = default;
stream_builder_t::stream_builder_t (stream_builder_t &&) noexcept = default;
stream_builder_t &stream_builder_t::operator= (stream_builder_t &&) noexcept = default;

stream_builder_t &stream_builder_t::bind (std::string endpoint)
{
    if (endpoint.empty ()) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "STREAM bind endpoint must not be empty");
    }
    _state->snapshot.bind_endpoint = std::move (endpoint);
    return *this;
}

stream_builder_t &stream_builder_t::register_session (std::string session_name)
{
    if (session_name.empty ()) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "STREAM packet session name must not be empty");
    }
    _state->snapshot.packet_session_name = std::move (session_name);
    return *this;
}

stream_builder_t &stream_builder_t::attach_actor_gateway (std::string spot_node_name)
{
    if (spot_node_name.empty ()) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "ActorGateway Spot node name must not be empty");
    }
    _state->snapshot.actor_gateway_spot_node_name = std::move (spot_node_name);
    return *this;
}

stream_snapshot_t stream_builder_t::snapshot () const
{
    return _state->snapshot;
}

zlink_builder_t &zlink_builder_t::stream (std::string stream_name,
                                          std::function<void (stream_builder_t &)> configure)
{
    auto state = std::make_shared<detail::stream_builder_state_t> (std::move (stream_name));
    stream_builder_t builder (state);
    if (configure) {
        configure (builder);
    }
    if (state->snapshot.bind_endpoint.empty () || state->snapshot.packet_session_name.empty ()) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "STREAM requires bind endpoint and packet session");
    }
    _state->stream_runtime->streams[state->snapshot.name] = state;
    return *this;
}

std::vector<stream_snapshot_t> zlink_builder_t::streams () const
{
    std::vector<stream_snapshot_t> result;
    result.reserve (_state->stream_runtime->streams.size ());
    for (const auto &[_, state] : _state->stream_runtime->streams) {
        result.push_back (state->snapshot);
    }
    return result;
}

} // namespace zlink::framework

namespace zlink::framework::detail
{

stream_runtime_t::stream_runtime_t (std::shared_ptr<stream_runtime_state_t> state) :
    _state (std::move (state))
{
}

stream_runtime_t stream_runtime_t::from (const zlink_builder_t &builder)
{
    return stream_runtime_t (builder._state->stream_runtime);
}

namespace
{

bool has_flag (stream_header_flags_t flags, stream_header_flags_t flag)
{
    return (static_cast<std::uint8_t> (flags) & static_cast<std::uint8_t> (flag)) != 0;
}

bool known_kind (stream_message_kind_t kind)
{
    switch (kind) {
        case stream_message_kind_t::send:
        case stream_message_kind_t::request:
        case stream_message_kind_t::response:
        case stream_message_kind_t::error:
        case stream_message_kind_t::control:
            return true;
    }
    return false;
}

bool known_codec (stream_codec_t codec)
{
    switch (codec) {
        case stream_codec_t::raw:
        case stream_codec_t::json:
        case stream_codec_t::message_pack:
        case stream_codec_t::protobuf:
            return true;
    }
    return false;
}

result_t<void> validate_name (std::string_view name, bool allow_reserved)
{
    if (name.empty () || name.size () > 255) {
        return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                        "STREAM packet name is invalid");
    }
    if (!allow_reserved && name.rfind ("__zlink.", 0) == 0) {
        return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                        "STREAM packet name uses a reserved prefix");
    }
    return result_t<void>::success ();
}

void append_u64 (std::vector<std::uint8_t> &bytes, std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back (static_cast<std::uint8_t> ((value >> shift) & 0xff));
    }
}

std::uint64_t read_u64 (const std::vector<std::uint8_t> &bytes, std::size_t &offset)
{
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | bytes[offset++];
    }
    return value;
}

} // namespace

result_t<void> stream_runtime_t::validate_header (const stream_header_t &header) const
{
    if (!known_kind (header.kind ()) || !known_codec (header.codec ())) {
        return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                        "STREAM header contains unknown kind or codec");
    }

    const auto raw_flags = static_cast<std::uint8_t> (header.flags ());
    constexpr auto known_flags =
      static_cast<std::uint8_t> (stream_header_flags_t::has_request_seq)
      | static_cast<std::uint8_t> (stream_header_flags_t::has_metadata)
      | static_cast<std::uint8_t> (stream_header_flags_t::payload_compressed);
    if ((raw_flags & ~known_flags) != 0) {
        return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                        "STREAM header contains unknown flags");
    }

    if (auto valid_name =
          validate_name (header.packet_name (), header.kind () == stream_message_kind_t::control);
        !valid_name) {
        return valid_name;
    }

    const bool has_request_seq = header.request_seq ().has_value ();
    const bool has_metadata = !header.metadata ().empty ();
    if (header.kind () == stream_message_kind_t::send && has_request_seq) {
        return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                        "STREAM send packet must not contain request sequence");
    }
    if ((header.kind () == stream_message_kind_t::request
         || header.kind () == stream_message_kind_t::response)
        && !has_request_seq) {
        return result_t<void>::failure (
          framework_error_kind_t::request_protocol_error,
          "STREAM request and response packets require request sequence");
    }
    if (has_request_seq && *header.request_seq () == 0) {
        return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                        "STREAM request sequence must not be zero");
    }
    if (header.kind () == stream_message_kind_t::error && header.codec () != stream_codec_t::json) {
        return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                        "STREAM error packet must use JSON codec");
    }
    if (header.kind () == stream_message_kind_t::control) {
        if (header.flags () != stream_header_flags_t::none || header.codec () != stream_codec_t::raw
            || has_request_seq || has_metadata) {
            return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                            "STREAM control packet must be raw and flagless");
        }
    }
    return result_t<void>::success ();
}

result_t<std::vector<std::uint8_t>>
stream_runtime_t::encode_header (const stream_header_t &header) const
{
    if (auto valid = validate_header (header); !valid) {
        return result_t<std::vector<std::uint8_t>>::failure (valid.error_kind (),
                                                             valid.error ()->what ());
    }

    auto flags = header.flags ();
    if (header.request_seq ()) {
        flags = flags | stream_header_flags_t::has_request_seq;
    }
    if (!header.metadata ().empty ()) {
        flags = flags | stream_header_flags_t::has_metadata;
    }

    std::vector<std::uint8_t> bytes;
    bytes.push_back (static_cast<std::uint8_t> (header.kind ()));
    bytes.push_back (static_cast<std::uint8_t> (header.codec ()));
    bytes.push_back (static_cast<std::uint8_t> (flags));
    if (header.request_seq ()) {
        append_u64 (bytes, *header.request_seq ());
    }
    bytes.push_back (static_cast<std::uint8_t> (header.packet_name ().size ()));
    bytes.insert (bytes.end (), header.packet_name ().begin (), header.packet_name ().end ());
    if (!header.metadata ().empty ()) {
        if (header.metadata ().values ().size () > std::numeric_limits<std::uint8_t>::max ()) {
            return result_t<std::vector<std::uint8_t>>::failure (
              framework_error_kind_t::request_protocol_error,
              "STREAM metadata item count is too large");
        }
        bytes.push_back (static_cast<std::uint8_t> (header.metadata ().values ().size ()));
        for (const auto &[key, value] : header.metadata ().values ()) {
            if (key.size () > 255 || value.size () > 255) {
                return result_t<std::vector<std::uint8_t>>::failure (
                  framework_error_kind_t::request_protocol_error,
                  "STREAM metadata key or value is too large");
            }
            bytes.push_back (static_cast<std::uint8_t> (key.size ()));
            bytes.insert (bytes.end (), key.begin (), key.end ());
            bytes.push_back (static_cast<std::uint8_t> (value.size ()));
            bytes.insert (bytes.end (), value.begin (), value.end ());
        }
    }
    return result_t<std::vector<std::uint8_t>>::success (std::move (bytes));
}

result_t<stream_header_t>
stream_runtime_t::decode_header (const std::vector<std::uint8_t> &bytes) const
{
    if (bytes.size () < 4) {
        return result_t<stream_header_t>::failure (framework_error_kind_t::payload_decode_failed,
                                                   "STREAM header is too short");
    }
    std::size_t offset = 0;
    const auto kind = static_cast<stream_message_kind_t> (bytes[offset++]);
    const auto codec = static_cast<stream_codec_t> (bytes[offset++]);
    auto flags = static_cast<stream_header_flags_t> (bytes[offset++]);
    std::optional<std::uint64_t> request_seq;
    if (has_flag (flags, stream_header_flags_t::has_request_seq)) {
        if (bytes.size () - offset < 8) {
            return result_t<stream_header_t>::failure (
              framework_error_kind_t::payload_decode_failed,
              "STREAM request sequence is incomplete");
        }
        request_seq = read_u64 (bytes, offset);
    }
    if (offset >= bytes.size ()) {
        return result_t<stream_header_t>::failure (framework_error_kind_t::payload_decode_failed,
                                                   "STREAM packet name length is missing");
    }
    const auto name_size = bytes[offset++];
    if (name_size == 0 || bytes.size () - offset < name_size) {
        return result_t<stream_header_t>::failure (framework_error_kind_t::payload_decode_failed,
                                                   "STREAM packet name is invalid");
    }
    std::string name (bytes.begin () + static_cast<std::ptrdiff_t> (offset),
                      bytes.begin () + static_cast<std::ptrdiff_t> (offset + name_size));
    offset += name_size;

    stream_metadata_t metadata;
    if (has_flag (flags, stream_header_flags_t::has_metadata)) {
        if (offset >= bytes.size ()) {
            return result_t<stream_header_t>::failure (
              framework_error_kind_t::payload_decode_failed, "STREAM metadata count is missing");
        }
        const auto count = bytes[offset++];
        for (std::uint8_t i = 0; i < count; ++i) {
            if (offset >= bytes.size ()) {
                return result_t<stream_header_t>::failure (
                  framework_error_kind_t::payload_decode_failed,
                  "STREAM metadata key length is missing");
            }
            const auto key_size = bytes[offset++];
            if (bytes.size () - offset < key_size) {
                return result_t<stream_header_t>::failure (
                  framework_error_kind_t::payload_decode_failed,
                  "STREAM metadata key is incomplete");
            }
            std::string key (bytes.begin () + static_cast<std::ptrdiff_t> (offset),
                             bytes.begin () + static_cast<std::ptrdiff_t> (offset + key_size));
            offset += key_size;
            if (offset >= bytes.size ()) {
                return result_t<stream_header_t>::failure (
                  framework_error_kind_t::payload_decode_failed,
                  "STREAM metadata value length is missing");
            }
            const auto value_size = bytes[offset++];
            if (bytes.size () - offset < value_size) {
                return result_t<stream_header_t>::failure (
                  framework_error_kind_t::payload_decode_failed,
                  "STREAM metadata value is incomplete");
            }
            std::string value (bytes.begin () + static_cast<std::ptrdiff_t> (offset),
                               bytes.begin () + static_cast<std::ptrdiff_t> (offset + value_size));
            offset += value_size;
            metadata.with (std::move (key), std::move (value));
        }
    }
    if (offset != bytes.size ()) {
        return result_t<stream_header_t>::failure (framework_error_kind_t::payload_decode_failed,
                                                   "STREAM header has trailing bytes");
    }

    stream_header_t header (kind, codec, flags, request_seq, std::move (name),
                            std::move (metadata));
    if (auto valid = validate_header (header); !valid) {
        return result_t<stream_header_t>::failure (framework_error_kind_t::payload_decode_failed,
                                                   valid.error ()->what ());
    }
    return result_t<stream_header_t>::success (std::move (header));
}

stream_t stream_runtime_t::open_session (std::string stream_name) const
{
    if (_state->streams.find (stream_name) == _state->streams.end ()) {
        throw framework_exception_t (framework_error_kind_t::request_target_not_found,
                                     "STREAM endpoint is not registered");
    }
    auto state = std::make_shared<stream_state_t> ();
    state->session_id = std::move (stream_name) + ":" + std::to_string (_state->next_session_id++);
    return stream_t (state);
}

result_t<void> stream_runtime_t::dispatch_serial (stream_t &stream,
                                                  std::string operation,
                                                  std::function<task_t<void> ()> callback) const
{
    return stream_session_dispatcher_t (*stream._state)
      .dispatch (std::move (operation), std::move (callback));
}

result_t<void> stream_runtime_t::dispatch_connected (packet_stream_session_t &session,
                                                     stream_t &stream) const
{
    return dispatch_serial (stream, "connected", [&] { return session.on_connected (stream); });
}

result_t<void> stream_runtime_t::dispatch_packet (packet_stream_session_t &session,
                                                  stream_t &stream,
                                                  const stream_header_t &header,
                                                  const zlink::message_t &payload) const
{
    if (auto valid = validate_header (header); !valid) {
        return valid;
    }
    return dispatch_serial (stream, "packet:" + std::string (header.packet_name ()),
                            [&] { return session.on_packet (stream, header, payload); });
}

result_t<void> stream_runtime_t::dispatch_disconnected (packet_stream_session_t &session,
                                                        stream_t &stream) const
{
    stream._state->closed = true;
    return dispatch_serial (stream, "disconnected",
                            [&] { return session.on_disconnected (stream); });
}

result_t<void> stream_runtime_t::dispatch_error (packet_stream_session_t &session,
                                                 stream_t &stream,
                                                 const stream_error_t &error) const
{
    return dispatch_serial (stream, "error", [&] { return session.on_error (stream, error); });
}

std::vector<std::string> stream_runtime_t::serial_log (const stream_t &stream) const
{
    return {stream._state->serial_log.begin (), stream._state->serial_log.end ()};
}

std::vector<stream_header_t> stream_runtime_t::written_headers (const stream_t &stream) const
{
    return stream._state->written_headers;
}

} // namespace zlink::framework::detail
