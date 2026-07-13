/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include <zlink/framework.hpp>

#include "runtime/streams/stream_runtime.hpp"

#include <condition_variable>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
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
        stream.reply_packet (payload).submit ();
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

class delayed_reply_session_t final : public zlink::framework::packet_stream_session_t
{
  public:
    delayed_reply_session_t () :
        _entered_future (_entered.get_future ()),
        _resume ([this] (std::function<void ()> continuation) {
            {
                std::lock_guard lock (_mutex);
                _continuations.push_back (std::move (continuation));
            }
            _continuation_ready.notify_one ();
        })
    {
    }

    zlink::framework::task_t<void> on_connected (zlink::framework::stream_t &) override
    {
        co_return;
    }

    zlink::framework::task_t<void> on_disconnected (zlink::framework::stream_t &) override
    {
        co_return;
    }

    zlink::framework::task_t<void> on_error (
      zlink::framework::stream_t &,
      const zlink::framework::stream_error_t &) override
    {
        co_return;
    }

    zlink::framework::task_t<void> on_packet (
      zlink::framework::stream_t &stream,
      const zlink::framework::stream_dispatch_context_t &,
      const zlink::message_t &payload) override
    {
        _entered.set_value ();
        co_await _resume.task ();
        try {
            stream.reply_packet (payload).submit ();
            reply_result = zlink::framework::result_t<void>::success ();
        }
        catch (const zlink::framework::framework_exception_t &error) {
            reply_result = zlink::framework::detail::result_access_t::failure<void> (error);
        }
    }

    void wait_until_suspended () { _entered_future.wait (); }

    void resume ()
    {
        _resume.complete (zlink::framework::result_t<void>::success ());
        std::function<void ()> continuation;
        {
            std::unique_lock lock (_mutex);
            _continuation_ready.wait (lock, [this] { return !_continuations.empty (); });
            continuation = std::move (_continuations.front ());
            _continuations.pop_front ();
        }
        continuation ();
    }

    std::optional<zlink::framework::result_t<void>> reply_result;

  private:
    std::promise<void> _entered;
    std::future<void> _entered_future;
    std::mutex _mutex;
    std::condition_variable _continuation_ready;
    std::deque<std::function<void ()>> _continuations;
    zlink::framework::detail::task_completion_source_t<void> _resume;
};

class prefix_stream_compression_codec_t final
  : public zlink::framework::stream_compression_codec_t
{
  public:
    explicit prefix_stream_compression_codec_t (std::string prefix) :
        _prefix (std::move (prefix))
    {
    }

    zlink::message_t compress (const zlink::message_t &payload) const override
    {
        return zlink::message_t::from (_prefix + payload.to_string ());
    }

    zlink::message_t decompress (const zlink::message_t &payload,
                                 std::size_t max_decompressed_size) const override
    {
        const auto text = payload.to_string ();
        if (text.rfind (_prefix, 0) != 0) {
            throw std::runtime_error ("custom stream compression marker is invalid");
        }
        auto decoded = text.substr (_prefix.size ());
        if (decoded.size () > max_decompressed_size) {
            throw std::runtime_error ("custom stream payload exceeds receive limit");
        }
        return zlink::message_t::from (decoded);
    }

  private:
    std::string _prefix;
};

class oversized_stream_compression_codec_t final
  : public zlink::framework::stream_compression_codec_t
{
  public:
    zlink::message_t compress (const zlink::message_t &payload) const override { return payload; }

    zlink::message_t decompress (const zlink::message_t &, std::size_t) const override
    {
        return zlink::message_t::from (std::string (64 * 1024 + 1, 'x'));
    }
};

} // namespace

int main ()
{
    using zlink::framework::framework_error_kind_t;
    using zlink::framework::stream_codec_t;
    using zlink::framework::detail::stream_header_flags_t;
    using zlink::framework::detail::stream_message_kind_t;

    struct stream_dispatch_executor_guard_t
    {
        stream_dispatch_executor_guard_t ()
        {
            zlink::framework::detail::configure_stream_dispatch_executor ();
        }

        ~stream_dispatch_executor_guard_t ()
        {
            zlink::framework::detail::shutdown_stream_dispatch_executor ();
        }
    } stream_dispatch_executor_guard;

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
      {0xF2, static_cast<std::uint8_t> (stream_message_kind_t::request),
       static_cast<std::uint8_t> (stream_codec_t::json),
       static_cast<std::uint8_t> (stream_header_flags_t::has_request_seq), 1},
      {0xF2, static_cast<std::uint8_t> (stream_message_kind_t::send),
       static_cast<std::uint8_t> (stream_codec_t::json),
       static_cast<std::uint8_t> (stream_header_flags_t::none), 0},
      {0xF2, static_cast<std::uint8_t> (stream_message_kind_t::send),
       static_cast<std::uint8_t> (stream_codec_t::json),
       static_cast<std::uint8_t> (stream_header_flags_t::none), 4, 'n'},
      {0xF2, static_cast<std::uint8_t> (stream_message_kind_t::send),
       static_cast<std::uint8_t> (stream_codec_t::json),
       static_cast<std::uint8_t> (stream_header_flags_t::has_metadata), 4, 'n', 'a', 'm', 'e'},
      {0xF2, static_cast<std::uint8_t> (stream_message_kind_t::send),
       static_cast<std::uint8_t> (stream_codec_t::json),
       static_cast<std::uint8_t> (stream_header_flags_t::has_metadata), 4, 'n', 'a', 'm', 'e', 1},
      {0xF2, static_cast<std::uint8_t> (stream_message_kind_t::send),
       static_cast<std::uint8_t> (stream_codec_t::json),
       static_cast<std::uint8_t> (stream_header_flags_t::has_metadata), 4, 'n', 'a', 'm', 'e', 1, 3,
       'k'},
      {0xF2, static_cast<std::uint8_t> (stream_message_kind_t::send),
       static_cast<std::uint8_t> (stream_codec_t::json),
       static_cast<std::uint8_t> (stream_header_flags_t::has_metadata), 4, 'n', 'a', 'm', 'e', 1, 1,
       'k'},
      {0xF2, static_cast<std::uint8_t> (stream_message_kind_t::send),
       static_cast<std::uint8_t> (stream_codec_t::json),
       static_cast<std::uint8_t> (stream_header_flags_t::has_metadata), 4, 'n', 'a', 'm', 'e', 1, 1,
       'k', 3, 'v'},
      {0xF2, static_cast<std::uint8_t> (stream_message_kind_t::send),
       static_cast<std::uint8_t> (stream_codec_t::json),
       static_cast<std::uint8_t> (stream_header_flags_t::none), 4, 'n', 'a', 'm', 'e', 0}};
    for (const auto &invalid_header : invalid_headers) {
        if (runtime.decode_header (invalid_header)
            || runtime.decode_header (invalid_header).error_kind ()
                 != framework_error_kind_t::payload_decode_failed) {
            return 22;
        }
    }

    /* flow-correlation §3.2/§3.4: missing/wrong marker and malformed flow
     * fields fail as protocol errors; a valid flow pair round-trips. */
    const std::vector<std::uint8_t> no_marker_header{
      static_cast<std::uint8_t> (stream_message_kind_t::send),
      static_cast<std::uint8_t> (stream_codec_t::json),
      static_cast<std::uint8_t> (stream_header_flags_t::none), 4, 'n', 'a', 'm', 'e'};
    auto no_marker = runtime.decode_header (no_marker_header);
    if (no_marker
        || no_marker.error_kind () != framework_error_kind_t::request_protocol_error) {
        return 220;
    }
    const std::vector<std::uint8_t> truncated_flow_header{
      0xF2, static_cast<std::uint8_t> (stream_message_kind_t::send),
      static_cast<std::uint8_t> (stream_codec_t::json),
      static_cast<std::uint8_t> (stream_header_flags_t::has_flow_id), 4, 'n', 'a', 'm', 'e', 'x'};
    if (runtime.decode_header (truncated_flow_header)) {
        return 221;
    }
    const std::string sample_flow_id = "01890a5d-ac96-774b-bcce-b302099a8057";
    zlink::framework::detail::stream_header_t flow_header (
      stream_message_kind_t::send, stream_codec_t::json, stream_header_flags_t::none,
      std::nullopt, "flowed", {});
    flow_header.with_flow (sample_flow_id, zlink::framework::flow_origin_t::inbound);
    auto flow_encoded = runtime.encode_header (flow_header);
    if (!flow_encoded || flow_encoded.value ()[0] != 0xF2) {
        return 222;
    }
    auto flow_decoded = runtime.decode_header (flow_encoded.value ());
    if (!flow_decoded || !flow_decoded.value ().flow_id ()
        || *flow_decoded.value ().flow_id () != sample_flow_id
        || flow_decoded.value ().flow_origin () != zlink::framework::flow_origin_t::inbound) {
        return 223;
    }
    if (runtime.encode_header (flow_decoded.value ())
          .value ()
          != flow_encoded.value ()) {
        return 224;
    }
    zlink::framework::detail::stream_header_t bad_flow_header (
      stream_message_kind_t::send, stream_codec_t::json, stream_header_flags_t::none,
      std::nullopt, "flowed", {});
    bad_flow_header.with_flow ("UPPERCASE-not-a-uuid7-value-000000000", // 37 bytes, invalid
                               zlink::framework::flow_origin_t::inbound);
    if (runtime.encode_header (bad_flow_header)) {
        return 225;
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
    /* stream connector §5.2: Response는 request의 packet name을 그대로 되돌린다. */
    if (runtime.written_headers (stream)[0].kind () != stream_message_kind_t::response
        || runtime.written_headers (stream)[0].request_seq () != 77
        || runtime.written_headers (stream)[0].packet_name () != "move") {
        return 15;
    }

    auto delayed_stream = runtime.open_session ("client-stream");
    delayed_reply_session_t delayed_session;
    std::optional<zlink::framework::result_t<void>> delayed_dispatch;
    std::thread delayed_dispatch_thread ([&] {
        delayed_dispatch = runtime.dispatch_packet (
          delayed_session, delayed_stream, request_header,
          zlink::message_t::from (std::string ("delayed-payload")));
    });
    delayed_session.wait_until_suspended ();
    delayed_session.resume ();
    delayed_dispatch_thread.join ();
    if (!delayed_dispatch || !*delayed_dispatch || !delayed_session.reply_result
        || !*delayed_session.reply_result || runtime.written_headers (delayed_stream).size () != 1
        || runtime.written_headers (delayed_stream)[0].request_seq () != 77
        || runtime.written_payloads (delayed_stream)[0].to_string () != "delayed-payload") {
        return 29;
    }

    auto fluent_stream = runtime.open_session ("client-stream");
    auto send_call =
      fluent_stream.write_packet (zlink::message_t::from (std::string ("send-payload")));
    send_call.packet_name ("original");
    if (!runtime.written_headers (fluent_stream).empty ()) {
        return 17;
    }
    send_call.metadata ("trace", "send-trace").packet_name ("renamed").compress ().submit ();
    if (runtime.written_headers (fluent_stream).size () != 1
        || runtime.written_headers (fluent_stream)[0].packet_name () != "renamed"
        || runtime.written_headers (fluent_stream)[0].metadata ("trace") != "send-trace"
        || (runtime.written_headers (fluent_stream)[0].flags ()
            & stream_header_flags_t::payload_compressed)
             != stream_header_flags_t::payload_compressed) {
        return 18;
    }
    if (runtime.written_payloads (fluent_stream).empty ()
        || runtime.written_payloads (fluent_stream)[0].to_string () == "send-payload") {
        return 23;
    }
    const auto close_result = fluent_stream.close ().result ();
    const auto write_rejected_disconnected = [] (auto &&write_fn) {
        try {
            write_fn ();
            return false;
        }
        catch (const zlink::framework::framework_exception_t &error) {
            return zlink::framework::detail::boundary_state (error) == zlink::framework::detail::boundary_error_t::disconnected;
        }
    };
    if (!write_rejected_disconnected ([&] {
            fluent_stream.write_packet (zlink::message_t::from (std::string ("after-close")))
              .submit ();
        })) {
        return 24;
    }
    if (!close_result || runtime.written_headers (fluent_stream).size () != 1) {
        return 19;
    }
    if (!write_rejected_disconnected ([&] {
            stream.write_packet (zlink::message_t::from (std::string ("after-disconnect")))
              .submit ();
        })) {
        return 25;
    }
    if (runtime.written_headers (stream).size () != 1) {
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

    auto custom_codec =
      std::make_shared<prefix_stream_compression_codec_t> ("custom-stream:");
    zlink::framework::service_collection_t custom_services;
    zlink::framework::handler_registry_t custom_handlers;
    zlink::framework::serializer_registry_t custom_serializers;
    zlink::framework::zlink_builder_t custom_zlink;
    zlink::framework::monitoring_builder_t custom_monitoring;
    zlink::framework::zlink_framework_options_t custom_options (
      custom_services, custom_handlers, custom_serializers, custom_zlink, custom_monitoring);
    custom_options.configure_stream_compression ().use (custom_codec);
    custom_options.add_stream_node ("custom-stream")
      .bind ("tcp://0.0.0.0:9201")
      .register_session ("custom-session");
    custom_options.apply ();
    auto custom_runtime = zlink::framework::detail::stream_runtime_t::from (custom_zlink);
    auto custom_stream = custom_runtime.open_session ("custom-stream");
    custom_stream.write_packet (zlink::message_t::from (std::string ("custom-outbound")))
      .compress ()
      .submit ();
    if (custom_runtime.written_payloads (custom_stream).size () != 1
        || custom_runtime.written_payloads (custom_stream)[0].to_string ()
             != "custom-stream:custom-outbound") {
        return 24;
    }
    zlink::framework::detail::stream_header_t custom_inbound_header (
      stream_message_kind_t::request, stream_codec_t::raw,
      stream_header_flags_t::has_request_seq | stream_header_flags_t::payload_compressed, 88,
      "custom-inbound");
    sample_session_t custom_session;
    const auto custom_dispatch = custom_runtime.dispatch_packet (
      custom_session, custom_stream, custom_inbound_header,
      custom_codec->compress (zlink::message_t::from (std::string ("custom-inbound-payload"))));
    if (!custom_dispatch || custom_session.events.size () != 1
        || custom_session.events[0] != "packet:custom-inbound:custom-inbound-payload") {
        return 25;
    }

    zlink::framework::zlink_builder_t disabled_zlink;
    zlink::framework::monitoring_builder_t disabled_monitoring;
    zlink::framework::zlink_framework_options_t disabled_options (
      custom_services, custom_handlers, custom_serializers, disabled_zlink, disabled_monitoring);
    disabled_options.configure_stream_compression ().disable ();
    disabled_options.add_stream_node ("disabled-stream")
      .bind ("tcp://0.0.0.0:9202")
      .register_session ("disabled-session");
    disabled_options.apply ();
    auto disabled_runtime = zlink::framework::detail::stream_runtime_t::from (disabled_zlink);
    auto disabled_stream = disabled_runtime.open_session ("disabled-stream");
    bool disabled_compress_rejected = false;
    try {
        disabled_stream.write_packet (zlink::message_t::from (std::string ("disabled")))
          .compress ()
          .submit ();
    }
    catch (const zlink::framework::framework_exception_t &) {
        disabled_compress_rejected = true;
    }
    if (!disabled_compress_rejected) {
        return 27;
    }
    sample_session_t disabled_session;
    const auto disabled_receive = disabled_runtime.dispatch_packet (
      disabled_session, disabled_stream, custom_inbound_header,
      custom_codec->compress (zlink::message_t::from (std::string ("disabled-inbound"))));
    if (disabled_receive
        || disabled_receive.error_kind () != framework_error_kind_t::payload_decode_failed
        || std::string (disabled_receive.error ()->what ()).find (
             "compression codec is not configured") == std::string::npos
        || !disabled_session.events.empty ()) {
        return 27;
    }

    zlink::framework::zlink_builder_t oversized_zlink;
    zlink::framework::monitoring_builder_t oversized_monitoring;
    zlink::framework::zlink_framework_options_t oversized_options (
      custom_services, custom_handlers, custom_serializers, oversized_zlink, oversized_monitoring);
    oversized_options.configure_stream_compression ().use (
      std::make_shared<oversized_stream_compression_codec_t> ());
    oversized_options.add_stream_node ("oversized-stream")
      .bind ("tcp://0.0.0.0:9203")
      .register_session ("oversized-session");
    oversized_options.apply ();
    auto oversized_runtime = zlink::framework::detail::stream_runtime_t::from (oversized_zlink);
    auto oversized_stream = oversized_runtime.open_session ("oversized-stream");
    sample_session_t oversized_session;
    const auto oversized_receive = oversized_runtime.dispatch_packet (
      oversized_session, oversized_stream, custom_inbound_header,
      zlink::message_t::from (std::string ("compressed")));
    if (oversized_receive
        || oversized_receive.error_kind () != framework_error_kind_t::payload_decode_failed
        || !oversized_session.events.empty ()) {
        return 28;
    }

    return 0;
}
