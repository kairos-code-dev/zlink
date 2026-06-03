/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/stream_connector.hpp>
#include <zlink/stream_connector/codecs/auto_codec.hpp>
#include <zlink/Contracts/Sockets/stream_socket.hpp>
#include <zlink/Contracts/Service/operation_contracts.hpp>

#include "runtime/connector_runtime.hpp"
#include "runtime/protocol/compression/lz4_compression_codec.hpp"
#include "runtime/protocol/framing/frame_codec.hpp"
#include "runtime/protocol/header_codec.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#ifdef ZLINK_STREAM_CONNECTOR_TEST_WITH_OPENSSL
#include <boost/asio/ssl/stream.hpp>
#endif

#include <chrono>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{

struct login_request_t
{
  static constexpr const char *packet_name = "LoginRequest";
};

struct login_reply_t
{
};

struct auto_payload_t
{
  static constexpr const char *packet_name = "AutoPayload";
  std::string text;
};

void
to_json (nlohmann::json &json, const auto_payload_t &payload)
{
  json = nlohmann::json { { "text", payload.text } };
}

void
from_json (const nlohmann::json &json, auto_payload_t &payload)
{
  payload.text = json.at ("text").get<std::string> ();
}

struct server_frame_t
{
  zlink::stream_connector::detail::stream_header_t header;
  std::string payload;
  bool compressed = false;
};

std::optional<server_frame_t>
try_read_server_frame (std::string &buffer)
{
  if (buffer.size () < 6) {
    return std::nullopt;
  }
  const auto header_size =
    (static_cast<std::uint8_t> (buffer[0]) << 8) |
    static_cast<std::uint8_t> (buffer[1]);
  const auto payload_size =
    (static_cast<std::size_t> (static_cast<std::uint8_t> (buffer[2])) << 24) |
    (static_cast<std::size_t> (static_cast<std::uint8_t> (buffer[3])) << 16) |
    (static_cast<std::size_t> (static_cast<std::uint8_t> (buffer[4])) << 8) |
    static_cast<std::uint8_t> (buffer[5]);
  if (buffer.size () < 6 + header_size + payload_size) {
    return std::nullopt;
  }
  std::vector<std::uint8_t> header_bytes (
    buffer.begin () + 6,
    buffer.begin () + 6 + static_cast<std::ptrdiff_t> (header_size));
  auto decoded =
    zlink::stream_connector::detail::header_codec_t {}.decode (header_bytes);
  if (!decoded) {
    return std::nullopt;
  }
  std::string payload (
    buffer.begin () + 6 + static_cast<std::ptrdiff_t> (header_size),
    buffer.begin () + 6 + static_cast<std::ptrdiff_t> (header_size) +
      static_cast<std::ptrdiff_t> (payload_size));
  buffer.erase (0, 6 + header_size + payload_size);
  const bool compressed =
    (static_cast<std::uint8_t> (decoded.value ().flags) &
     static_cast<std::uint8_t> (
       zlink::stream_connector::header_flags_t::payload_compressed)) != 0;
  if (compressed) {
    payload =
      zlink::stream_connector::detail::lz4_compression_codec_t {}
        .decompress (zlink::message_t::from (payload))
        .to_string ();
  }
  return server_frame_t { decoded.value (), std::move (payload), compressed };
}

zlink::message_t
make_server_frame (zlink::stream_connector::message_kind_t kind,
                   std::uint64_t seq,
                   std::string name,
                   std::string payload,
                   bool compressed = false)
{
  zlink::stream_connector::detail::stream_header_t header;
  header.kind = kind;
  header.codec = zlink::stream_connector::codec_t::raw;
  header.flags = compressed
                   ? zlink::stream_connector::header_flags_t::payload_compressed
                   : zlink::stream_connector::header_flags_t::none;
  header.request_seq =
    kind == zlink::stream_connector::message_kind_t::request ||
        kind == zlink::stream_connector::message_kind_t::response ||
        kind == zlink::stream_connector::message_kind_t::error
      ? std::optional<std::uint64_t> { seq }
      : std::optional<std::uint64_t> {};
  header.name = std::move (name);
  auto header_bytes =
    zlink::stream_connector::detail::header_codec_t {}.encode (header);
  zlink::stream_connector::connector_options_t options;
  options.compression = zlink::stream_connector::compression_t::lz4;
  if (compressed) {
    payload =
      zlink::stream_connector::detail::lz4_compression_codec_t {}
        .compress (zlink::message_t::from (payload))
        .to_string ();
  }
  std::vector<std::uint8_t> payload_bytes (payload.begin (), payload.end ());
  options.max_send_payload_size =
    std::max (options.max_send_payload_size, payload_bytes.size ());
  auto frame = zlink::stream_connector::detail::frame_codec_t::encode (
    header_bytes.value (), payload_bytes, options);
  return zlink::message_t::from (
    std::string (frame.value ().begin (), frame.value ().end ()));
}

} // namespace

int
main ()
{
  {
    zlink::stream_connector::detail::lz4_compression_codec_t lz4;
    if (!lz4.available ()) {
      return 19;
    }
    const auto source = zlink::message_t::from (
      std::string ("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    const auto compressed = lz4.compress (source);
    const auto restored = lz4.decompress (compressed);
    if (restored.to_string () != source.to_string () ||
        compressed.to_string () == source.to_string ()) {
      return 20;
    }
  }

  {
    zlink::stream_connector::detail::header_codec_t header_codec;
    zlink::stream_connector::detail::stream_header_t header;
    header.kind = zlink::stream_connector::message_kind_t::request;
    header.codec = zlink::stream_connector::codec_t::json;
    header.request_seq = 42;
    header.name = "profile.get";
    header.metadata.with ("traceId", "abc");
    auto encoded = header_codec.encode (header);
    if (!encoded) {
      return 21;
    }
    auto decoded = header_codec.decode (encoded.value ());
    if (!decoded ||
        decoded.value ().kind !=
          zlink::stream_connector::message_kind_t::request ||
        decoded.value ().codec != zlink::stream_connector::codec_t::json ||
        decoded.value ().request_seq.value_or (0) != 42 ||
        decoded.value ().name != "profile.get" ||
        decoded.value ().metadata.values.at ("traceId") != "abc") {
      return 22;
    }
    const std::vector<std::uint8_t> invalid_flag { 1, 1, 0x80, 1,
                                                   static_cast<std::uint8_t> ('x') };
    if (header_codec.decode (invalid_flag).error_code () !=
        zlink::stream_connector::error_code_t::frame_decode_failed) {
      return 23;
    }
    zlink::stream_connector::detail::stream_header_t control;
    control.kind = zlink::stream_connector::message_kind_t::control;
    control.codec = zlink::stream_connector::codec_t::raw;
    control.name = "$zlink.heartbeat.ping";
    if (!header_codec.encode (control)) {
      return 24;
    }
    control.codec = zlink::stream_connector::codec_t::json;
    if (header_codec.encode (control) ||
        header_codec.encode (control).error_code () !=
          zlink::stream_connector::error_code_t::frame_decode_failed) {
      return 25;
    }
    zlink::stream_connector::connector_options_t frame_options;
    frame_options.max_send_payload_size = 16;
    auto frame = zlink::stream_connector::detail::frame_codec_t::encode (
      encoded.value (),
      std::vector<std::uint8_t> { 'o', 'k' },
      frame_options);
    if (!frame || frame.value ().size () != encoded.value ().size () + 8 ||
        frame.value ()[0] != 0 ||
        frame.value ()[1] != encoded.value ().size () ||
        frame.value ()[2] != 0 || frame.value ()[3] != 0 ||
        frame.value ()[4] != 0 || frame.value ()[5] != 2) {
      return 26;
    }
  }

  zlink::context_t context;
  zlink::stream_socket_t server (context);
  server.options ().notify (false);
  server.bind ("tcp://127.0.0.1:0");
  const auto endpoint = server.options ().last_endpoint ();
  std::atomic_bool compressed_send_seen { false };
  std::thread server_thread ([&server, &compressed_send_seen] {
    int handled = 0;
    std::string buffer;
    while (handled < 2) {
      zlink::received_t inbound;
      if (server.recv (inbound) != 0) {
        return;
      }
      buffer += inbound.parts ().empty () ? std::string {}
                                          : inbound.parts ()[0].to_string ();
      while (auto frame = try_read_server_frame (buffer)) {
        if (frame->header.kind ==
              zlink::stream_connector::message_kind_t::send &&
            frame->compressed &&
            frame->payload == "{}") {
          compressed_send_seen = true;
        }
        if (frame->header.kind ==
            zlink::stream_connector::message_kind_t::request) {
          auto push = make_server_frame (
            zlink::stream_connector::message_kind_t::send,
            0,
            "server.compressed",
            "server-payload",
            true);
          inbound.send ().message (push).submit ();
          auto reply = make_server_frame (
            zlink::stream_connector::message_kind_t::response,
            frame->header.request_seq.value (),
            "reply",
            "ok");
          inbound.send ().message (reply).submit ();
        }
        ++handled;
      }
      inbound.close ();
    }
  });

  zlink::stream_connector::connector_options_t options;
  options.endpoint = endpoint;
  options.dispatch_mode = zlink::stream_connector::dispatch_mode_t::manual;
  options.max_send_payload_size = 16;
  options.compression = zlink::stream_connector::compression_t::lz4;

  auto connector =
    zlink::stream_connector::connector_factory_t::create (options);
  if (connector.state () !=
      zlink::stream_connector::connection_state_t::created) {
    return 1;
  }

  std::vector<zlink::stream_connector::connection_state_t> states;
  connector.on_connection_state_changed (
    [&](const zlink::stream_connector::connection_state_changed_t &state) {
      states.push_back (state.current);
    });

  if (!connector.connect ().result () || !connector.is_connected () ||
      states.size () != 2 ||
      states.back () != zlink::stream_connector::connection_state_t::connected) {
    return 2;
  }

  connector.codecs ().add_json<login_request_t> ();
  if (!connector.codecs ().supports (zlink::stream_connector::codec_t::json)) {
    return 3;
  }

  bool callback_seen = false;
  connector
    .send (login_request_t {})
    .metadata ("trace", "t1")
    .compress ()
    .submit ([&](zlink::stream_connector::result_t<void> result) {
      callback_seen = static_cast<bool> (result);
    });
  if (!callback_seen) {
    return 4;
  }
  auto runtime =
    zlink::stream_connector::detail::connector_runtime_t::from (connector);
  if (runtime.sent_packets ().size () != 1 ||
      runtime.sent_packets ()[0].name != login_request_t::packet_name ||
      runtime.sent_packets ()[0].codec !=
        zlink::stream_connector::codec_t::json ||
      runtime.sent_packets ()[0].metadata.values.at ("trace") != "t1") {
    return 5;
  }

  auto request =
    connector.request<login_reply_t> (login_request_t {})
      .packet_name ("login.request")
      .timeout (std::chrono::milliseconds (5))
      .submit ()
      .result ();
  if (!request || runtime.pending_request_count () != 0) {
    return 6;
  }
  server_thread.join ();
  if (!compressed_send_seen) {
    return 27;
  }

  int compressed_dispatch_count = 0;
  connector.on<zlink::stream_connector::packet_t> (
    "server.compressed",
    [&](const zlink::stream_connector::packet_t &packet) {
      if (packet.compressed &&
          packet.payload.to_string () == "server-payload") {
        ++compressed_dispatch_count;
      }
    });
  if (connector.pending_dispatch_count () != 1) {
    return 28;
  }
  if (!connector.dispatch ().result () || compressed_dispatch_count != 1 ||
      connector.pending_dispatch_count () != 0) {
    return 29;
  }

  zlink::stream_socket_t receive_server (context);
  receive_server.options ().notify (false);
  receive_server.bind ("tcp://127.0.0.1:0");
  const auto receive_endpoint = receive_server.options ().last_endpoint ();
  std::thread receive_server_thread ([&receive_server] {
    zlink::received_t inbound;
    if (receive_server.recv (inbound) != 0) {
      return;
    }
    auto first = make_server_frame (
      zlink::stream_connector::message_kind_t::send,
      0,
      "server.receive.one",
      "one");
    auto second = make_server_frame (
      zlink::stream_connector::message_kind_t::send,
      0,
      "server.receive.two",
      "two");
    inbound.send ()
      .message (zlink::message_t::from (
        first.to_string () + second.to_string ()))
      .submit ();
    inbound.close ();
  });
  zlink::stream_connector::connector_options_t receive_options;
  receive_options.endpoint = receive_endpoint;
  receive_options.dispatch_mode =
    zlink::stream_connector::dispatch_mode_t::manual;
  receive_options.request_timeout = std::chrono::milliseconds (100);
  auto receive_connector =
    zlink::stream_connector::connector_factory_t::create (receive_options);
  if (!receive_connector.connect ().result ()) {
    return 56;
  }
  if (!receive_connector.send (login_request_t {})
         .packet_name ("receive.trigger")
         .submit ()
         .result ()) {
    return 57;
  }
  auto received_first =
    receive_connector.receive (std::chrono::milliseconds (100)).result ();
  auto received_second =
    receive_connector.receive (std::chrono::milliseconds (100)).result ();
  receive_server_thread.join ();
  if (!received_first || !received_second ||
      received_first.value ().name != "server.receive.one" ||
      received_first.value ().payload.to_string () != "one" ||
      received_second.value ().name != "server.receive.two" ||
      received_second.value ().payload.to_string () != "two" ||
      receive_connector.pending_dispatch_count () != 0) {
    return 58;
  }
  receive_connector.close ().result ();

  auto oversized_payload =
    connector
      .send (zlink::stream_connector::packet_t {
        "oversized.payload",
        {},
        zlink::stream_connector::codec_t::raw,
        false,
        zlink::message_t::from (std::string (17, 'x')) })
      .submit ()
      .result ();
  if (oversized_payload ||
      oversized_payload.error_code () !=
        zlink::stream_connector::error_code_t::frame_too_large) {
    return 40;
  }

  zlink::stream_connector::metadata_t oversized_metadata;
  oversized_metadata.with ("trace", std::string (9 * 1024, 'm'));
  auto oversized_metadata_result =
    connector
      .send (zlink::stream_connector::packet_t {
        "oversized.metadata",
        std::move (oversized_metadata),
        zlink::stream_connector::codec_t::raw,
        false,
        zlink::message_t::from (std::string ("ok")) })
      .submit ()
      .result ();
  if (oversized_metadata_result ||
      oversized_metadata_result.error_code () !=
        zlink::stream_connector::error_code_t::validation_failed) {
    return 41;
  }

  int manual_dispatch_count = 0;
  connector.on<zlink::stream_connector::packet_t> (
    "server.push",
    [&](const zlink::stream_connector::packet_t &packet) {
      if (packet.payload.to_string () == "payload") {
        ++manual_dispatch_count;
      }
    });
  runtime.receive_packet (zlink::stream_connector::packet_t {
    "server.push",
    {},
    zlink::stream_connector::codec_t::raw,
    false,
    zlink::message_t::from (std::string ("payload")) });
  if (manual_dispatch_count != 0 || connector.pending_dispatch_count () != 1) {
    return 7;
  }
  if (!connector.dispatch ().result () || manual_dispatch_count != 1 ||
      connector.pending_dispatch_count () != 0) {
    return 8;
  }

  zlink::stream_connector::connector_options_t immediate_options;
  immediate_options.endpoint = endpoint;
  immediate_options.dispatch_mode =
    zlink::stream_connector::dispatch_mode_t::immediate;
  auto immediate =
    zlink::stream_connector::connector_factory_t::create (immediate_options);
  if (!immediate.connect ().result ()) {
    return 9;
  }
  int immediate_count = 0;
  immediate.on<zlink::stream_connector::packet_t> (
    "server.push",
    [&](const zlink::stream_connector::packet_t &) { ++immediate_count; });
  zlink::stream_connector::detail::connector_runtime_t::from (immediate)
    .receive_packet (zlink::stream_connector::packet_t {
      "server.push",
      {},
      zlink::stream_connector::codec_t::raw,
      false,
      zlink::message_t::from (std::string ("payload")) });
  if (immediate_count != 1 || immediate.pending_dispatch_count () != 0) {
    return 10;
  }

  const auto message_pack_supported = connector.codecs ().supports (
    zlink::stream_connector::codec_t::message_pack);
  bool error_seen = false;
  try {
    connector.codecs ().add_message_pack<login_request_t> ();
  } catch (const std::invalid_argument &) {
    error_seen = true;
  }
  if (message_pack_supported == error_seen) {
    return 11;
  }
  auto disconnected = connector.close ().result ();
  if (!disconnected || connector.state () !=
                         zlink::stream_connector::connection_state_t::closed) {
    return 12;
  }

  auto send_after_close =
    connector.send (login_request_t {}).packet_name ("after.close").submit ().result ();
  if (send_after_close ||
      send_after_close.error_code () !=
        zlink::stream_connector::error_code_t::disconnected) {
    return 13;
  }
  bool request_after_close_callback_seen = false;
  connector.request<login_reply_t> (login_request_t {})
    .packet_name ("after.close.request")
    .submit ([&](zlink::stream_connector::result_t<login_reply_t> result) {
      request_after_close_callback_seen =
        !result &&
        result.error_code () ==
          zlink::stream_connector::error_code_t::disconnected;
    });
  if (!request_after_close_callback_seen) {
    return 59;
  }
  auto missing_endpoint =
    zlink::stream_connector::connector_factory_t::create (
      zlink::stream_connector::connector_options_t {});
  if (missing_endpoint.connect ().result () ||
      missing_endpoint.connect ().result ().error_code () !=
        zlink::stream_connector::error_code_t::configuration_error) {
    return 14;
  }

  zlink::stream_socket_t auto_server (context);
  auto_server.options ().notify (false);
  auto_server.bind ("tcp://127.0.0.1:0");
  const auto auto_endpoint = auto_server.options ().last_endpoint ();
  std::atomic_bool auto_json_seen { false };
  std::thread auto_server_thread ([&auto_server, &auto_json_seen] {
    zlink::received_t inbound;
    if (auto_server.recv (inbound) != 0) {
      return;
    }
    std::string buffer = inbound.parts ().empty ()
                           ? std::string {}
                           : inbound.parts ()[0].to_string ();
    if (auto frame = try_read_server_frame (buffer)) {
      auto_json_seen =
        frame->header.kind == zlink::stream_connector::message_kind_t::send &&
        frame->header.codec == zlink::stream_connector::codec_t::json &&
        frame->header.name == auto_payload_t::packet_name &&
        nlohmann::json::parse (frame->payload).at ("text").get<std::string> ()
          == "auto";
    }
    inbound.close ();
  });

  zlink::stream_connector::connector_options_t auto_options;
  auto_options.endpoint = auto_endpoint;
  auto_options.dispatch_mode =
    zlink::stream_connector::dispatch_mode_t::manual;
  auto auto_connector =
    zlink::stream_connector::connector_factory_t::create (auto_options);
  if (!auto_connector.connect ().result ()) {
    return 30;
  }
  if (!zlink::stream_connector::codecs::send (
         auto_connector, auto_payload_t { "auto" })
         .submit ()
         .result ()) {
    return 31;
  }
  auto_server_thread.join ();
  if (!auto_json_seen) {
    return 32;
  }

  int auto_dispatch_count = 0;
  zlink::stream_connector::codecs::on<auto_payload_t> (
    auto_connector,
    [&](const auto_payload_t &payload) {
      if (payload.text == "callback") {
        ++auto_dispatch_count;
      }
    });
  zlink::stream_connector::detail::connector_runtime_t::from (auto_connector)
    .receive_packet (zlink::stream_connector::packet_t {
      auto_payload_t::packet_name,
      {},
      zlink::stream_connector::codec_t::json,
      false,
      zlink::message_t::from_json (auto_payload_t { "callback" }) });
  if (auto_connector.pending_dispatch_count () != 1 ||
      auto_dispatch_count != 0) {
    return 33;
  }
  if (!auto_connector.dispatch ().result () || auto_dispatch_count != 1 ||
      auto_connector.pending_dispatch_count () != 0) {
    return 34;
  }
  auto_connector.close ().result ();

  zlink::stream_socket_t timeout_server (context);
  timeout_server.options ().notify (false);
  timeout_server.bind ("tcp://127.0.0.1:0");
  const auto timeout_endpoint = timeout_server.options ().last_endpoint ();
  std::atomic_bool timed_request_seen { false };
  std::thread timeout_server_thread ([&timeout_server, &timed_request_seen] {
    zlink::received_t inbound;
    if (timeout_server.recv (inbound) != 0) {
      return;
    }
    timed_request_seen = true;
    inbound.close ();
  });
  zlink::stream_connector::connector_options_t timeout_options;
  timeout_options.endpoint = timeout_endpoint;
  timeout_options.request_timeout = std::chrono::milliseconds (5);
  auto timeout_connector =
    zlink::stream_connector::connector_factory_t::create (timeout_options);
  if (!timeout_connector.connect ().result ()) {
    return 35;
  }
  auto timeout_reply =
    timeout_connector.request<login_reply_t> (login_request_t {})
      .packet_name ("timeout.request")
      .timeout (std::chrono::milliseconds (5))
      .submit ()
      .result ();
  timeout_server_thread.join ();
  if (timeout_reply ||
      timeout_reply.error_code () !=
        zlink::stream_connector::error_code_t::request_timeout ||
      !timed_request_seen) {
    return 36;
  }
  timeout_connector.close ().result ();

  zlink::stream_socket_t callback_response_server (context);
  callback_response_server.options ().notify (false);
  callback_response_server.bind ("tcp://127.0.0.1:0");
  const auto callback_response_endpoint =
    callback_response_server.options ().last_endpoint ();
  std::thread callback_response_thread ([&callback_response_server] {
    zlink::received_t inbound;
    if (callback_response_server.recv (inbound) != 0) {
      return;
    }
    std::string buffer = inbound.parts ().empty ()
                           ? std::string {}
                           : inbound.parts ()[0].to_string ();
    if (auto frame = try_read_server_frame (buffer)) {
      auto reply = make_server_frame (
        zlink::stream_connector::message_kind_t::response,
        frame->header.request_seq.value (),
        "reply",
        "ok");
      inbound.send ().message (reply).submit ();
    }
    inbound.close ();
  });
  zlink::stream_connector::connector_options_t callback_response_options;
  callback_response_options.endpoint = callback_response_endpoint;
  auto callback_response_connector =
    zlink::stream_connector::connector_factory_t::create (
      callback_response_options);
  if (!callback_response_connector.connect ().result ()) {
    return 60;
  }
  bool request_callback_response_seen = false;
  callback_response_connector.request<login_reply_t> (login_request_t {})
    .packet_name ("callback.response.request")
    .timeout (std::chrono::milliseconds (100))
    .submit ([&](zlink::stream_connector::result_t<login_reply_t> result) {
      request_callback_response_seen = static_cast<bool> (result);
    });
  callback_response_thread.join ();
  if (!request_callback_response_seen) {
    return 61;
  }
  callback_response_connector.close ().result ();

  zlink::stream_socket_t callback_timeout_server (context);
  callback_timeout_server.options ().notify (false);
  callback_timeout_server.bind ("tcp://127.0.0.1:0");
  const auto callback_timeout_endpoint =
    callback_timeout_server.options ().last_endpoint ();
  std::atomic_bool callback_timeout_request_seen { false };
  std::thread callback_timeout_thread (
    [&callback_timeout_server, &callback_timeout_request_seen] {
      zlink::received_t inbound;
      if (callback_timeout_server.recv (inbound) != 0) {
        return;
      }
      callback_timeout_request_seen = true;
      inbound.close ();
    });
  zlink::stream_connector::connector_options_t callback_timeout_options;
  callback_timeout_options.endpoint = callback_timeout_endpoint;
  callback_timeout_options.request_timeout = std::chrono::milliseconds (5);
  auto callback_timeout_connector =
    zlink::stream_connector::connector_factory_t::create (
      callback_timeout_options);
  if (!callback_timeout_connector.connect ().result ()) {
    return 62;
  }
  bool request_callback_timeout_seen = false;
  callback_timeout_connector.request<login_reply_t> (login_request_t {})
    .packet_name ("callback.timeout.request")
    .timeout (std::chrono::milliseconds (5))
    .submit ([&](zlink::stream_connector::result_t<login_reply_t> result) {
      request_callback_timeout_seen =
        !result &&
        result.error_code () ==
          zlink::stream_connector::error_code_t::request_timeout;
    });
  callback_timeout_thread.join ();
  if (!request_callback_timeout_seen || !callback_timeout_request_seen) {
    return 63;
  }
  callback_timeout_connector.close ().result ();

  boost::asio::io_context partial_io;
  boost::asio::ip::tcp::acceptor partial_acceptor (
    partial_io,
    { boost::asio::ip::make_address ("127.0.0.1"), 0 });
  const auto partial_endpoint =
    std::string ("tcp://127.0.0.1:") +
    std::to_string (partial_acceptor.local_endpoint ().port ());
  std::atomic_bool partial_write_seen { false };
  std::thread partial_server_thread (
    [&partial_acceptor, &partial_write_seen] {
      boost::asio::ip::tcp::socket socket (
        partial_acceptor.get_executor ());
      partial_acceptor.accept (socket);
      std::array<char, 256> request_buffer {};
      boost::system::error_code error;
      socket.read_some (boost::asio::buffer (request_buffer), error);
      if (error) {
        return;
      }
      const auto frame = make_server_frame (
                           zlink::stream_connector::message_kind_t::send,
                           0,
                           "server.partial",
                           "split")
                           .to_string ();
      socket.write_some (boost::asio::buffer (frame.data (), 3), error);
      if (error) {
        return;
      }
      std::this_thread::sleep_for (std::chrono::milliseconds (2));
      socket.write_some (
        boost::asio::buffer (frame.data () + 3, frame.size () - 3),
        error);
      partial_write_seen = !error;
    });
  zlink::stream_connector::connector_options_t partial_options;
  partial_options.endpoint = partial_endpoint;
  partial_options.dispatch_mode =
    zlink::stream_connector::dispatch_mode_t::manual;
  auto partial_connector =
    zlink::stream_connector::connector_factory_t::create (partial_options);
  if (!partial_connector.connect ().result ()) {
    return 64;
  }
  if (!partial_connector.send (login_request_t {})
         .packet_name ("partial.trigger")
         .submit ()
         .result ()) {
    return 65;
  }
  auto partial_packet =
    partial_connector.receive (std::chrono::milliseconds (100)).result ();
  partial_server_thread.join ();
  if (!partial_packet || !partial_write_seen ||
      partial_packet.value ().name != "server.partial" ||
      partial_packet.value ().payload.to_string () != "split") {
    return 66;
  }
  partial_connector.close ().result ();

  zlink::stream_socket_t large_receive_server (context);
  large_receive_server.options ().notify (false);
  large_receive_server.bind ("tcp://127.0.0.1:0");
  const auto large_receive_endpoint =
    large_receive_server.options ().last_endpoint ();
  const std::string large_receive_payload (70 * 1024, 'l');
  std::thread large_receive_server_thread (
    [&large_receive_server, &large_receive_payload] {
      zlink::received_t inbound;
      if (large_receive_server.recv (inbound) != 0) {
        return;
      }
      auto frame = make_server_frame (
        zlink::stream_connector::message_kind_t::send,
        0,
        "server.large",
        large_receive_payload);
      inbound.send ().message (frame).submit ();
      inbound.close ();
    });
  zlink::stream_connector::connector_options_t large_receive_options;
  large_receive_options.endpoint = large_receive_endpoint;
  large_receive_options.dispatch_mode =
    zlink::stream_connector::dispatch_mode_t::manual;
  large_receive_options.max_send_payload_size = 16;
  auto large_receive_connector =
    zlink::stream_connector::connector_factory_t::create (
      large_receive_options);
  if (!large_receive_connector.connect ().result ()) {
    return 70;
  }
  if (!large_receive_connector.send (login_request_t {})
         .packet_name ("large.receive.trigger")
         .submit ()
         .result ()) {
    return 71;
  }
  auto large_received =
    large_receive_connector.receive (std::chrono::milliseconds (100)).result ();
  large_receive_server_thread.join ();
  if (!large_received ||
      large_received.value ().name != "server.large" ||
      large_received.value ().payload.to_string ().size () !=
        large_receive_payload.size ()) {
    return 72;
  }
  large_receive_connector.close ().result ();

  zlink::stream_socket_t heartbeat_server (context);
  heartbeat_server.options ().notify (false);
  heartbeat_server.bind ("tcp://127.0.0.1:0");
  const auto heartbeat_endpoint = heartbeat_server.options ().last_endpoint ();
  std::atomic_bool heartbeat_seen { false };
  std::thread heartbeat_server_thread ([&heartbeat_server, &heartbeat_seen] {
    zlink::received_t inbound;
    if (heartbeat_server.recv (inbound) != 0) {
      return;
    }
    std::string buffer = inbound.parts ().empty ()
                           ? std::string {}
                           : inbound.parts ()[0].to_string ();
    if (auto frame = try_read_server_frame (buffer)) {
      heartbeat_seen =
        frame->header.kind == zlink::stream_connector::message_kind_t::control &&
        frame->header.name == "$zlink.heartbeat.ping";
      auto pong = make_server_frame (
        zlink::stream_connector::message_kind_t::control,
        0,
        "$zlink.heartbeat.pong",
        "");
      inbound.send ().message (pong).submit ();
    }
    inbound.close ();
  });
  zlink::stream_connector::connector_options_t heartbeat_options;
  heartbeat_options.endpoint = heartbeat_endpoint;
  heartbeat_options.heartbeat.interval = std::chrono::milliseconds (0);
  auto heartbeat_connector =
    zlink::stream_connector::connector_factory_t::create (heartbeat_options);
  if (!heartbeat_connector.connect ().result () ||
      !heartbeat_connector.dispatch ().result ()) {
    return 37;
  }
  heartbeat_server_thread.join ();
  if (!heartbeat_seen) {
    return 38;
  }
  bool heartbeat_control_delivered = false;
  heartbeat_connector.on<zlink::stream_connector::packet_t> (
    "$zlink.heartbeat.pong",
    [&](const zlink::stream_connector::packet_t &) {
      heartbeat_control_delivered = true;
    });
  if (!heartbeat_connector.dispatch ().result () ||
      heartbeat_control_delivered ||
      heartbeat_connector.pending_dispatch_count () != 0) {
    return 44;
  }
  heartbeat_connector.close ().result ();

  zlink::stream_socket_t heartbeat_timeout_server (context);
  heartbeat_timeout_server.options ().notify (false);
  heartbeat_timeout_server.bind ("tcp://127.0.0.1:0");
  zlink::stream_connector::connector_options_t heartbeat_timeout_options;
  heartbeat_timeout_options.endpoint =
    heartbeat_timeout_server.options ().last_endpoint ();
  heartbeat_timeout_options.heartbeat.timeout = std::chrono::milliseconds (0);
  auto heartbeat_timeout_connector =
    zlink::stream_connector::connector_factory_t::create (
      heartbeat_timeout_options);
  if (!heartbeat_timeout_connector.connect ().result ()) {
    return 45;
  }
  auto heartbeat_timeout_result =
    heartbeat_timeout_connector.dispatch ().result ();
  if (heartbeat_timeout_result ||
      heartbeat_timeout_result.error_code () !=
        zlink::stream_connector::error_code_t::disconnected ||
      heartbeat_timeout_connector.state () !=
        zlink::stream_connector::connection_state_t::disconnected) {
    return 46;
  }

  boost::asio::io_context websocket_io;
  boost::asio::ip::tcp::acceptor websocket_acceptor (
    websocket_io,
    { boost::asio::ip::make_address ("127.0.0.1"), 0 });
  const auto websocket_endpoint =
    std::string ("ws://127.0.0.1:") +
    std::to_string (websocket_acceptor.local_endpoint ().port ()) +
    "/stream";
  std::atomic_bool websocket_send_seen { false };
  std::thread websocket_server_thread (
    [&websocket_acceptor, &websocket_send_seen] {
      boost::asio::ip::tcp::socket socket (
        websocket_acceptor.get_executor ());
      websocket_acceptor.accept (socket);
      boost::beast::websocket::stream<boost::asio::ip::tcp::socket> websocket (
        std::move (socket));
      websocket.accept ();
      boost::beast::flat_buffer buffer;
      websocket.read (buffer);
      auto frame_text = boost::beast::buffers_to_string (buffer.data ());
      if (auto frame = try_read_server_frame (frame_text)) {
        websocket_send_seen =
          frame->header.kind ==
            zlink::stream_connector::message_kind_t::send &&
          frame->header.name == login_request_t::packet_name &&
          websocket.got_binary ();
      }
      boost::system::error_code ignored;
      websocket.close (
        boost::beast::websocket::close_code::normal, ignored);
    });

  zlink::stream_connector::connector_options_t websocket_options;
  websocket_options.endpoint = websocket_endpoint;
  websocket_options.transport = zlink::stream_connector::transport_t::websocket;
  auto websocket_connector =
    zlink::stream_connector::connector_factory_t::create (websocket_options);
  if (!websocket_connector.connect ().result ()) {
    return 47;
  }
  if (!websocket_connector.send (login_request_t {}).submit ().result ()) {
    return 48;
  }
  websocket_connector.close ().result ();
  websocket_server_thread.join ();
  if (!websocket_send_seen) {
    return 49;
  }

#ifdef ZLINK_STREAM_CONNECTOR_TEST_WITH_OPENSSL
  boost::asio::io_context tls_io;
  boost::asio::ip::tcp::acceptor tls_acceptor (
    tls_io,
    { boost::asio::ip::make_address ("127.0.0.1"), 0 });
  const auto tls_endpoint =
    std::string ("tls://localhost:") +
    std::to_string (tls_acceptor.local_endpoint ().port ());
  std::atomic_bool tls_send_seen { false };
  std::thread tls_server_thread ([&tls_acceptor, &tls_send_seen] {
    boost::asio::ssl::context tls_context (
      boost::asio::ssl::context::tls_server);
    tls_context.use_certificate_chain_file (
      ZLINK_STREAM_CONNECTOR_TEST_CERT);
    tls_context.use_private_key_file (
      ZLINK_STREAM_CONNECTOR_TEST_KEY,
      boost::asio::ssl::context::pem);
    boost::asio::ip::tcp::socket socket (tls_acceptor.get_executor ());
    tls_acceptor.accept (socket);
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> stream (
      std::move (socket),
      tls_context);
    boost::system::error_code error;
    stream.handshake (boost::asio::ssl::stream_base::server, error);
    if (error) {
      return;
    }
    std::string buffer;
    std::array<char, 1024> chunk {};
    while (!tls_send_seen) {
      const auto read = stream.read_some (
        boost::asio::buffer (chunk), error);
      if (error) {
        return;
      }
      buffer.append (chunk.data (), read);
      if (auto frame = try_read_server_frame (buffer)) {
        tls_send_seen =
          frame->header.kind ==
            zlink::stream_connector::message_kind_t::send &&
          frame->header.name == login_request_t::packet_name;
      }
    }
    stream.shutdown (error);
  });

  zlink::stream_connector::connector_options_t tls_options;
  tls_options.endpoint = tls_endpoint;
  tls_options.transport = zlink::stream_connector::transport_t::tls;
  tls_options.skip_server_certificate_validation = true;
  auto tls_connector =
    zlink::stream_connector::connector_factory_t::create (tls_options);
  if (!tls_connector.connect ().result ()) {
    return 50;
  }
  if (!tls_connector.send (login_request_t {}).submit ().result ()) {
    return 51;
  }
  tls_connector.close ().result ();
  tls_server_thread.join ();
  if (!tls_send_seen) {
    return 52;
  }

  boost::asio::io_context wss_io;
  boost::asio::ip::tcp::acceptor wss_acceptor (
    wss_io,
    { boost::asio::ip::make_address ("127.0.0.1"), 0 });
  const auto wss_endpoint =
    std::string ("wss://localhost:") +
    std::to_string (wss_acceptor.local_endpoint ().port ()) +
    "/stream";
  std::atomic_bool wss_send_seen { false };
  std::thread wss_server_thread ([&wss_acceptor, &wss_send_seen] {
    boost::asio::ssl::context tls_context (
      boost::asio::ssl::context::tls_server);
    tls_context.use_certificate_chain_file (
      ZLINK_STREAM_CONNECTOR_TEST_CERT);
    tls_context.use_private_key_file (
      ZLINK_STREAM_CONNECTOR_TEST_KEY,
      boost::asio::ssl::context::pem);
    boost::asio::ip::tcp::socket socket (wss_acceptor.get_executor ());
    wss_acceptor.accept (socket);
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> tls_stream (
      std::move (socket),
      tls_context);
    boost::system::error_code error;
    tls_stream.handshake (boost::asio::ssl::stream_base::server, error);
    if (error) {
      return;
    }
    boost::beast::websocket::stream<
      boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>
      websocket (std::move (tls_stream));
    websocket.accept ();
    boost::beast::flat_buffer buffer;
    websocket.read (buffer);
    auto frame_text = boost::beast::buffers_to_string (buffer.data ());
    if (auto frame = try_read_server_frame (frame_text)) {
      wss_send_seen =
        frame->header.kind ==
          zlink::stream_connector::message_kind_t::send &&
        frame->header.name == login_request_t::packet_name &&
        websocket.got_binary ();
    }
    websocket.close (
      boost::beast::websocket::close_code::normal, error);
  });

  zlink::stream_connector::connector_options_t wss_options;
  wss_options.endpoint = wss_endpoint;
  wss_options.transport =
    zlink::stream_connector::transport_t::websocket_secure;
  wss_options.skip_server_certificate_validation = true;
  auto wss_connector =
    zlink::stream_connector::connector_factory_t::create (wss_options);
  if (!wss_connector.connect ().result ()) {
    return 53;
  }
  if (!wss_connector.send (login_request_t {}).submit ().result ()) {
    return 54;
  }
  wss_connector.close ().result ();
  wss_server_thread.join ();
  if (!wss_send_seen) {
    return 55;
  }
#endif

  boost::asio::io_context reconnect_success_io;
  boost::asio::ip::tcp::acceptor reserved_reconnect_acceptor (
    reconnect_success_io);
  reserved_reconnect_acceptor.open (boost::asio::ip::tcp::v4 ());
  reserved_reconnect_acceptor.set_option (
    boost::asio::socket_base::reuse_address (true));
  reserved_reconnect_acceptor.bind (
    { boost::asio::ip::make_address ("127.0.0.1"), 0 });
  const auto reconnect_success_port =
    reserved_reconnect_acceptor.local_endpoint ().port ();
  reserved_reconnect_acceptor.close ();
  const auto reconnect_success_endpoint =
    std::string ("tcp://127.0.0.1:") +
    std::to_string (reconnect_success_port);
  std::atomic_bool reconnect_success_connecting { false };
  std::atomic_bool reconnect_success_send_seen { false };
  std::thread reconnect_success_server_thread (
    [&reconnect_success_io,
     reconnect_success_port,
     &reconnect_success_connecting,
     &reconnect_success_send_seen] {
      while (!reconnect_success_connecting) {
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
      }
      std::this_thread::sleep_for (std::chrono::milliseconds (20));
      boost::asio::ip::tcp::acceptor acceptor (reconnect_success_io);
      acceptor.open (boost::asio::ip::tcp::v4 ());
      acceptor.set_option (boost::asio::socket_base::reuse_address (true));
      acceptor.bind (
        { boost::asio::ip::make_address ("127.0.0.1"),
          reconnect_success_port });
      acceptor.listen ();
      boost::asio::ip::tcp::socket socket (acceptor.get_executor ());
      acceptor.accept (socket);
      std::array<char, 512> request_buffer {};
      boost::system::error_code error;
      const auto read_size =
        socket.read_some (boost::asio::buffer (request_buffer), error);
      if (error) {
        return;
      }
      std::string frame_text (request_buffer.data (), read_size);
      if (auto frame = try_read_server_frame (frame_text)) {
        reconnect_success_send_seen =
          frame->header.kind ==
            zlink::stream_connector::message_kind_t::send &&
          frame->header.name == login_request_t::packet_name;
      }
    });
  zlink::stream_connector::connector_options_t reconnect_success_options;
  reconnect_success_options.endpoint = reconnect_success_endpoint;
  reconnect_success_options.reconnect.initial_delay =
    std::chrono::milliseconds (10);
  reconnect_success_options.reconnect.max_delay =
    std::chrono::milliseconds (10);
  reconnect_success_options.reconnect.max_attempts = 4;
  auto reconnect_success_connector =
    zlink::stream_connector::connector_factory_t::create (
      reconnect_success_options);
  std::vector<zlink::stream_connector::connection_state_t>
    reconnect_success_states;
  reconnect_success_connector.on_connection_state_changed (
    [&](const zlink::stream_connector::connection_state_changed_t &state) {
      reconnect_success_states.push_back (state.current);
      if (state.current ==
          zlink::stream_connector::connection_state_t::connecting) {
        reconnect_success_connecting = true;
      }
    });
  if (!reconnect_success_connector.connect ().result () ||
      std::find (reconnect_success_states.begin (),
                 reconnect_success_states.end (),
                 zlink::stream_connector::connection_state_t::reconnecting) ==
        reconnect_success_states.end () ||
      reconnect_success_states.back () !=
        zlink::stream_connector::connection_state_t::connected) {
    return 67;
  }
  if (!reconnect_success_connector.send (login_request_t {}).submit ().result ()) {
    return 68;
  }
  reconnect_success_connector.close ().result ();
  reconnect_success_server_thread.join ();
  if (!reconnect_success_send_seen) {
    return 69;
  }

  zlink::stream_connector::connector_options_t reconnect_options;
  reconnect_options.endpoint = "tcp://127.0.0.1:1";
  reconnect_options.reconnect.initial_delay = std::chrono::milliseconds (1);
  reconnect_options.reconnect.max_delay = std::chrono::milliseconds (1);
  reconnect_options.reconnect.max_attempts = 2;
  auto reconnect_connector =
    zlink::stream_connector::connector_factory_t::create (reconnect_options);
  std::vector<zlink::stream_connector::connection_state_t> reconnect_states;
  reconnect_connector.on_connection_state_changed (
    [&](const zlink::stream_connector::connection_state_changed_t &state) {
      reconnect_states.push_back (state.current);
    });
  auto reconnect_result = reconnect_connector.connect ().result ();
  if (reconnect_result ||
      reconnect_result.error_code () !=
        zlink::stream_connector::error_code_t::connect_timeout ||
      std::find (reconnect_states.begin (),
                 reconnect_states.end (),
                 zlink::stream_connector::connection_state_t::reconnecting) ==
        reconnect_states.end ()) {
    return 39;
  }

  zlink::stream_connector::connector_options_t invalid_transport_options;
  invalid_transport_options.endpoint = endpoint;
  invalid_transport_options.transport =
    zlink::stream_connector::transport_t::websocket_secure;
  auto invalid_transport =
    zlink::stream_connector::connector_factory_t::create (
      invalid_transport_options);
  auto invalid_transport_result = invalid_transport.connect ().result ();
  if (invalid_transport_result ||
      invalid_transport_result.error_code () !=
        zlink::stream_connector::error_code_t::configuration_error ||
      invalid_transport.is_connected ()) {
    return 42;
  }

  auto request_after_reconnect_failure =
    reconnect_connector.request<login_reply_t> (login_request_t {})
      .packet_name ("after.reconnect.failure")
      .submit ()
      .result ();
  if (request_after_reconnect_failure ||
      request_after_reconnect_failure.error_code () !=
        zlink::stream_connector::error_code_t::disconnected) {
    return 43;
  }
  return 0;
}
