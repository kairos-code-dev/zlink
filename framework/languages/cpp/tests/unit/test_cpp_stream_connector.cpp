/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/stream_connector.hpp>
#include <zlink/stream_connector/codecs/auto_codec.hpp>
#include <zlink/Contracts/Sockets/stream_socket.hpp>
#include <zlink/Contracts/Service/operation_contracts.hpp>

#include "runtime/connector_runtime.hpp"
#include "runtime/protocol/compression/lz4_compression_codec.hpp"
#include "runtime/protocol/framing/frame_codec.hpp"
#include "runtime/protocol/header_codec.hpp"

#include <chrono>
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
  header.request_seq = kind == zlink::stream_connector::message_kind_t::send
                         ? std::optional<std::uint64_t> {}
                         : std::optional<std::uint64_t> { seq };
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
  if (!connector.codecs ().supports (zlink::stream_connector::codec_t::json) ||
      connector.codecs ().supports (
        zlink::stream_connector::codec_t::message_pack)) {
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

  bool error_seen = false;
  try {
    connector.codecs ().add_message_pack<login_request_t> ();
  } catch (const std::invalid_argument &) {
    error_seen = true;
  }
  if (!error_seen) {
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
  return 0;
}
