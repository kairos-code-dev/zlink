/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>

#include "runtime/streams/stream_runtime.hpp"

#include <string>
#include <vector>

namespace
{

class sample_session_t final : public zlink::framework::packet_stream_session_t
{
  public:
    zlink::framework::task_t<void> on_connected (zlink::framework::stream_t &stream) override
    {
        events.push_back ("connected:" + stream.session_id ());
        return zlink::framework::task_t<void> (zlink::framework::result_t<void>::success ());
    }

    zlink::framework::task_t<void> on_disconnected (zlink::framework::stream_t &stream) override
    {
        events.push_back ("disconnected:" + stream.session_id ());
        return zlink::framework::task_t<void> (zlink::framework::result_t<void>::success ());
    }

    zlink::framework::task_t<void> on_error (zlink::framework::stream_t &,
                                             const zlink::framework::stream_error_t &error) override
    {
        events.push_back ("error:" + std::string (error.message ()));
        return zlink::framework::task_t<void> (zlink::framework::result_t<void>::success ());
    }

    zlink::framework::task_t<void> on_packet (zlink::framework::stream_t &stream,
                                              const zlink::framework::stream_dispatch_context_t &dispatch,
                                              const zlink::message_t &payload) override
    {
        events.push_back ("packet:" + std::string (dispatch.packet_name ()) + ":"
                          + payload.to_string ());
        auto write = stream.reply_packet (payload).async ().result ();
        if (!write) {
            return zlink::framework::task_t<void> (write);
        }
        return zlink::framework::task_t<void> (zlink::framework::result_t<void>::success ());
    }

    std::vector<std::string> events;
};

class throwing_packet_session_t final : public zlink::framework::packet_stream_session_t
{
  public:
    zlink::framework::task_t<void> on_connected (zlink::framework::stream_t &) override
    {
        return zlink::framework::task_t<void> (zlink::framework::result_t<void>::success ());
    }

    zlink::framework::task_t<void> on_disconnected (zlink::framework::stream_t &) override
    {
        return zlink::framework::task_t<void> (zlink::framework::result_t<void>::success ());
    }

    zlink::framework::task_t<void> on_error (zlink::framework::stream_t &,
                                             const zlink::framework::stream_error_t &) override
    {
        on_error_called = true;
        return zlink::framework::task_t<void> (zlink::framework::result_t<void>::success ());
    }

    zlink::framework::task_t<void> on_packet (zlink::framework::stream_t &,
                                              const zlink::framework::stream_dispatch_context_t &,
                                              const zlink::message_t &) override
    {
        return zlink::framework::task_t<void> (zlink::framework::result_t<void>::failure (
          zlink::framework::framework_error_kind_t::request_failed, "application packet failure"));
    }

    bool on_error_called = false;
};

} // namespace

int main ()
{
    using zlink::framework::framework_error_kind_t;
    using zlink::framework::stream_codec_t;
    using zlink::framework::detail::stream_header_flags_t;
    using zlink::framework::detail::stream_message_kind_t;

    zlink::framework::zlink_builder_t zlink;
    zlink.stream ("client-stream")
      .bind ("tcp://0.0.0.0:9200")
      .register_session ("client");
    zlink::framework::serializer_registry_t serializers;
    serializers.add<std::string> (
      [] (const std::string &value) {
          return zlink::framework::encoded_payload_t::from_string (value);
      },
      [] (const zlink::framework::encoded_payload_t &payload) { return payload.to_string (); });
    zlink::framework::detail::bind_stream_serializers (zlink, serializers);

    const auto snapshots = zlink.streams ();
    if (snapshots.size () != 1 || snapshots[0].name != "client-stream"
        || snapshots[0].bind_endpoint != "tcp://0.0.0.0:9200"
        || snapshots[0].packet_session_name != "client")
        return 1;

    auto runtime = zlink::framework::detail::stream_runtime_t::from (zlink);
    zlink::framework::stream_metadata_t metadata;
    metadata.with ("trace", "42").with ("content_type", "application/json");
    zlink::framework::detail::stream_header_t request_header (
      stream_message_kind_t::request, stream_codec_t::json, stream_header_flags_t::has_request_seq,
      77, "move", metadata);
    // correlation_id is now a first-class stream-header field (not a metadata key).
    request_header.with_correlation_id ("abc");

    const auto encoded = runtime.encode_header (request_header);
    if (!encoded) {
        return 2;
    }
    const auto decoded = runtime.decode_header (encoded.value ());
    if (!decoded || decoded.value ().kind () != stream_message_kind_t::request
        || decoded.value ().codec () != stream_codec_t::json
        || decoded.value ().request_seq () != 77 || decoded.value ().packet_name () != "move"
        || decoded.value ().correlation_id () != "abc"
        || decoded.value ().content_type () != "application/json"
        || decoded.value ().metadata ("trace") != "42") {
        return 3;
    }

    zlink::framework::detail::stream_header_t invalid_send (
      stream_message_kind_t::send, stream_codec_t::json, stream_header_flags_t::has_request_seq, 1,
      "bad");
    if (runtime.validate_header (invalid_send)
        || runtime.validate_header (invalid_send).error_kind ()
             != framework_error_kind_t::request_protocol_error) {
        return 4;
    }

    zlink::framework::detail::stream_header_t reserved (stream_message_kind_t::send, stream_codec_t::raw,
                                                stream_header_flags_t::none, std::nullopt,
                                                "__zlink.internal");
    if (runtime.validate_header (reserved)) {
        return 5;
    }

    zlink::framework::detail::stream_header_t valid_control (
      stream_message_kind_t::control, stream_codec_t::raw, stream_header_flags_t::none,
      std::nullopt, "__zlink.ping");
    if (!runtime.validate_header (valid_control)) {
        return 6;
    }
    zlink::framework::detail::stream_header_t missing_request_seq (
      stream_message_kind_t::request, stream_codec_t::json, stream_header_flags_t::none,
      std::nullopt, "missing-seq");
    zlink::framework::detail::stream_header_t zero_request_seq (
      stream_message_kind_t::request, stream_codec_t::json, stream_header_flags_t::has_request_seq,
      0, "zero-seq");
    zlink::framework::detail::stream_header_t invalid_error (
      stream_message_kind_t::error, stream_codec_t::raw, stream_header_flags_t::has_request_seq, 1,
      "error");
    zlink::framework::detail::stream_header_t invalid_control (
      stream_message_kind_t::control, stream_codec_t::json, stream_header_flags_t::none,
      std::nullopt, "__zlink.bad");
    if (runtime.validate_header (missing_request_seq) || runtime.validate_header (zero_request_seq)
        || runtime.validate_header (invalid_error) || runtime.validate_header (invalid_control)) {
        return 20;
    }
    zlink::framework::stream_metadata_t large_metadata;
    large_metadata.with ("trace", std::string (65536, 'x'));
    zlink::framework::detail::stream_header_t too_large_metadata (
      stream_message_kind_t::send, stream_codec_t::json, stream_header_flags_t::none, std::nullopt,
      "large", large_metadata);
    if (runtime.encode_header (too_large_metadata)
        || runtime.encode_header (too_large_metadata).error_kind ()
             != framework_error_kind_t::request_protocol_error) {
        return 21;
    }
    const std::vector<std::vector<std::uint8_t>> invalid_headers{
      {},
      {static_cast<std::uint8_t> (stream_message_kind_t::request),
       static_cast<std::uint8_t> (stream_codec_t::json),
       static_cast<std::uint8_t> (stream_header_flags_t::has_request_seq), 1},
      {static_cast<std::uint8_t> (stream_message_kind_t::send),
       static_cast<std::uint8_t> (stream_codec_t::json),
       static_cast<std::uint8_t> (stream_header_flags_t::none), 0},
      {static_cast<std::uint8_t> (stream_message_kind_t::send),
       static_cast<std::uint8_t> (stream_codec_t::json),
       static_cast<std::uint8_t> (stream_header_flags_t::none), 4, 'n'},
      {static_cast<std::uint8_t> (stream_message_kind_t::send),
       static_cast<std::uint8_t> (stream_codec_t::json),
       static_cast<std::uint8_t> (stream_header_flags_t::has_metadata), 4, 'n', 'a', 'm', 'e'},
      {static_cast<std::uint8_t> (stream_message_kind_t::send),
       static_cast<std::uint8_t> (stream_codec_t::json),
       static_cast<std::uint8_t> (stream_header_flags_t::has_metadata), 4, 'n', 'a', 'm', 'e', 1},
      {static_cast<std::uint8_t> (stream_message_kind_t::send),
       static_cast<std::uint8_t> (stream_codec_t::json),
       static_cast<std::uint8_t> (stream_header_flags_t::has_metadata), 4, 'n', 'a', 'm', 'e', 1, 3,
       'k'},
      {static_cast<std::uint8_t> (stream_message_kind_t::send),
       static_cast<std::uint8_t> (stream_codec_t::json),
       static_cast<std::uint8_t> (stream_header_flags_t::has_metadata), 4, 'n', 'a', 'm', 'e', 1, 1,
       'k'},
      {static_cast<std::uint8_t> (stream_message_kind_t::send),
       static_cast<std::uint8_t> (stream_codec_t::json),
       static_cast<std::uint8_t> (stream_header_flags_t::has_metadata), 4, 'n', 'a', 'm', 'e', 1, 1,
       'k', 3, 'v'},
      {static_cast<std::uint8_t> (stream_message_kind_t::send),
       static_cast<std::uint8_t> (stream_codec_t::json),
       static_cast<std::uint8_t> (stream_header_flags_t::none), 4, 'n', 'a', 'm', 'e', 0}};
    for (const auto &invalid_header : invalid_headers) {
        if (runtime.decode_header (invalid_header)
            || runtime.decode_header (invalid_header).error_kind ()
                 != framework_error_kind_t::payload_decode_failed) {
            return 22;
        }
    }

    auto stream = runtime.open_session ("client-stream");
    sample_session_t session;
    if (!runtime.dispatch_connected (session, stream)) {
        return 7;
    }
    if (!runtime.dispatch_packet (session, stream, request_header,
                                  zlink::message_t::from (std::string ("payload")))) {
        return 8;
    }
    if (!runtime.dispatch_disconnected (session, stream)) {
        return 9;
    }
    const auto log = runtime.serial_log (stream);
    if (log.size () != 3 || log[0] != "connected" || log[1] != "packet:move"
        || log[2] != "disconnected") {
        return 10;
    }
    if (session.events.size () != 3 || session.events[1] != "packet:move:payload"
        || runtime.written_headers (stream).size () != 1) {
        return 11;
    }
    if (runtime.written_headers (stream)[0].kind () != stream_message_kind_t::response
        || runtime.written_headers (stream)[0].request_seq () != 77
        || runtime.written_headers (stream)[0].packet_name () != "reply") {
        return 15;
    }

    auto fluent_stream = runtime.open_session ("client-stream");
    auto send_call =
      fluent_stream.write_packet (zlink::message_t::from (std::string ("send-payload")));
    send_call.packet_name ("original");
    if (!runtime.written_headers (fluent_stream).empty ()) {
        return 17;
    }
    const auto send_result = send_call.metadata ("trace", "send-trace")
                               .packet_name ("renamed")
                               .compress ()
                               .async ().result ();
    if (!send_result || runtime.written_headers (fluent_stream).size () != 1
        || runtime.written_headers (fluent_stream)[0].packet_name () != "renamed"
        || runtime.written_headers (fluent_stream)[0].metadata ("trace") != "send-trace"
        || (runtime.written_headers (fluent_stream)[0].flags ()
            & stream_header_flags_t::payload_compressed)
             != stream_header_flags_t::payload_compressed) {
        return 18;
    }
    const auto close_result = fluent_stream.close ().result ();
    const auto close_write =
      fluent_stream.write_packet (zlink::message_t::from (std::string ("after-close")))
        .async ().result ();
    if (!close_result || close_write
        || close_write.error_kind () != framework_error_kind_t::disconnected
        || runtime.written_headers (fluent_stream).size () != 1) {
        return 19;
    }
    const auto disconnected_write =
      stream
        .write_packet (zlink::message_t::from (std::string ("after-disconnect")))
        .async ().result ();
    if (disconnected_write
        || disconnected_write.error_kind () != framework_error_kind_t::disconnected
        || runtime.written_headers (stream).size () != 1) {
        return 16;
    }

    sample_session_t validation_session;
    auto validation_stream = runtime.open_session ("client-stream");
    const auto rejected =
      runtime.dispatch_packet (validation_session, validation_stream, invalid_send,
                               zlink::message_t::from (std::string ("bad")));
    if (rejected || rejected.error_kind () != framework_error_kind_t::request_protocol_error
        || !validation_session.events.empty ()) {
        return 12;
    }

    throwing_packet_session_t throwing_session;
    auto throwing_stream = runtime.open_session ("client-stream");
    const auto handler_failure =
      runtime.dispatch_packet (throwing_session, throwing_stream, request_header,
                               zlink::message_t::from (std::string ("payload")));
    if (handler_failure || handler_failure.error_kind () != framework_error_kind_t::request_failed
        || throwing_session.on_error_called) {
        return 13;
    }

    sample_session_t error_session;
    auto error_stream = runtime.open_session ("client-stream");
    const auto transport_error = runtime.dispatch_error (
      error_session, error_stream,
      zlink::framework::stream_error_t (zlink::framework::stream_session_error_t::transport_error,
                                        100, "transport"));
    if (!transport_error || error_session.events.size () != 1
        || error_session.events[0] != "error:transport") {
        return 14;
    }

    return 0;
}
