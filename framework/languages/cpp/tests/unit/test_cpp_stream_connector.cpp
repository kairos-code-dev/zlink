/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/stream_connector.hpp>

#include "runtime/connector_runtime.hpp"

#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

struct login_request_t
{
};

struct login_reply_t
{
};

} // namespace

int
main ()
{
  zlink::stream_connector::connector_options_t options;
  options.endpoint = "tcp://127.0.0.1:9300";
  options.dispatch_mode = zlink::stream_connector::dispatch_mode_t::manual;
  options.max_send_payload_size = 16;

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
    .packet_name ("login")
    .metadata ("trace", "t1")
    .submit ([&](zlink::stream_connector::result_t<void> result) {
      callback_seen = static_cast<bool> (result);
    });
  if (!callback_seen) {
    return 4;
  }
  auto runtime =
    zlink::stream_connector::detail::connector_runtime_t::from (connector);
  if (runtime.sent_packets ().size () != 1 ||
      runtime.sent_packets ()[0].name != "login" ||
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
  if (request ||
      request.error_code () !=
        zlink::stream_connector::error_code_t::request_timeout ||
      runtime.pending_request_count () != 1) {
    return 6;
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
  immediate_options.endpoint = "tcp://127.0.0.1:9301";
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
  return 0;
}
