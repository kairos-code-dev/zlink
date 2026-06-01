/* SPDX-License-Identifier: MPL-2.0 */

#include "connector_runtime.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace zlink::stream_connector::detail
{

namespace
{

bool
is_connected (const connector_state_t &state)
{
  return state.state == connection_state_t::connected;
}

result_t<void>
validate_packet_limits (const connector_state_t &state, const packet_t &packet)
{
  if (packet.payload.size () > state.options.max_send_payload_size) {
    return result_t<void>::failure (error_code_t::frame_too_large,
                                    "stream connector payload is too large");
  }
  std::size_t metadata_size = 0;
  for (const auto &[key, value] : packet.metadata.values) {
    metadata_size += key.size () + value.size ();
  }
  if (metadata_size > state.options.max_metadata_size) {
    return result_t<void>::failure (error_code_t::validation_failed,
                                    "stream connector metadata is too large");
  }
  if (packet.codec == codec_t::message_pack && !state.message_pack_enabled) {
    return result_t<void>::failure (error_code_t::unsupported_codec,
                                    "MessagePack codec is not enabled");
  }
  if (packet.codec == codec_t::protobuf && !state.protobuf_enabled) {
    return result_t<void>::failure (error_code_t::unsupported_codec,
                                    "Protobuf codec is not enabled");
  }
  if (packet.compressed && state.options.compression == compression_t::lz4 &&
      !state.lz4_enabled) {
    return result_t<void>::failure (error_code_t::compression_failed,
                                    "LZ4 compression is not enabled");
  }
  return result_t<void>::success ();
}

void
publish_error (const connector_state_t &state, const error_t &error)
{
  for (const auto &handler : state.error_handlers) {
    handler (error);
  }
}

void
dispatch_packet (connector_state_t &state, const packet_t &packet)
{
  const auto found = state.packet_handlers.find (packet.name);
  if (found == state.packet_handlers.end ()) {
    return;
  }
  for (const auto &handler : found->second) {
    handler (packet);
  }
}

} // namespace

connector_runtime_t::connector_runtime_t (
  std::shared_ptr<connector_state_t> state)
  : _state (std::move (state))
{
}

connector_runtime_t
connector_runtime_t::from (const connector_t &connector)
{
  return connector_runtime_t (connector._state);
}

void
connector_runtime_t::receive_packet (packet_t packet)
{
  if (_state->options.dispatch_mode == dispatch_mode_t::immediate) {
    dispatch_packet (*_state, packet);
    return;
  }
  _state->dispatch_queue.push_back (std::move (packet));
}

void
connector_runtime_t::complete_next_request (zlink::message_t payload)
{
  if (_state->pending_requests.empty ()) {
    return;
  }
  _state->pending_requests.erase (_state->pending_requests.begin ());
  _state->dispatch_queue.push_back (packet_t {
    "reply",
    {},
    codec_t::raw,
    false,
    std::move (payload) });
}

const std::vector<packet_t> &
connector_runtime_t::sent_packets () const noexcept
{
  return _state->sent_packets;
}

std::size_t
connector_runtime_t::pending_request_count () const noexcept
{
  return _state->pending_requests.size ();
}

void
change_state (std::shared_ptr<connector_state_t> state,
              connection_state_t next,
              std::optional<error_t> error)
{
  const auto previous = state->state;
  state->state = next;
  connection_state_changed_t changed { previous, next, error };
  for (const auto &handler : state->state_handlers) {
    handler (changed);
  }
  if (next == connection_state_t::disconnected ||
      next == connection_state_t::closed) {
    for (const auto &handler : state->disconnected_handlers) {
      handler ();
    }
  }
}

result_t<void>
submit_send (std::shared_ptr<connector_state_t> state, packet_t packet)
{
  if (!is_connected (*state)) {
    return result_t<void>::failure (error_code_t::disconnected,
                                    "stream connector is not connected");
  }
  if (auto validation = validate_packet_limits (*state, packet); !validation) {
    publish_error (*state, *validation.error ());
    return validation;
  }
  state->sent_packets.push_back (std::move (packet));
  return result_t<void>::success ();
}

result_t<zlink::message_t>
submit_request (std::shared_ptr<connector_state_t> state,
                packet_t packet,
                std::chrono::milliseconds timeout)
{
  (void) timeout;
  if (!is_connected (*state)) {
    return result_t<zlink::message_t>::failure (
      error_code_t::disconnected,
      "stream connector is not connected");
  }
  if (auto validation = validate_packet_limits (*state, packet); !validation) {
    publish_error (*state, *validation.error ());
    return result_t<zlink::message_t>::failure (
      validation.error_code (),
      validation.error () ? validation.error ()->message
                          : "stream request validation failed");
  }
  const auto seq = state->next_request_seq++;
  state->pending_requests.emplace (
    seq, pending_request_t { seq, std::move (packet) });
  return result_t<zlink::message_t>::failure (
    error_code_t::request_timeout,
    "stream request reply was not completed by the local test runtime");
}

result_t<void>
dispatch_pending (std::shared_ptr<connector_state_t> state)
{
  while (!state->dispatch_queue.empty ()) {
    auto packet = std::move (state->dispatch_queue.front ());
    state->dispatch_queue.pop_front ();
    dispatch_packet (*state, packet);
  }
  return result_t<void>::success ();
}

} // namespace zlink::stream_connector::detail

namespace zlink::stream_connector
{

codec_registry_t::codec_registry_t ()
  : _state (std::make_shared<detail::connector_state_t> (
      connector_options_t {}))
{
}

codec_registry_t::codec_registry_t (
  std::shared_ptr<detail::connector_state_t> state)
  : _state (std::move (state))
{
}

codec_registry_t::~codec_registry_t () = default;
codec_registry_t::codec_registry_t (codec_registry_t &&) noexcept = default;
codec_registry_t &codec_registry_t::operator= (
  codec_registry_t &&) noexcept = default;

codec_registry_t &
codec_registry_t::add_erased (std::type_index type, codec_t codec)
{
  if (!supports (codec)) {
    throw std::invalid_argument ("stream connector codec is not enabled");
  }
  _state->codecs[type] = codec;
  return *this;
}

bool
codec_registry_t::supports (codec_t codec) const
{
  switch (codec) {
  case codec_t::raw:
    return true;
  case codec_t::json:
    return _state->json_enabled;
  case codec_t::message_pack:
    return _state->message_pack_enabled;
  case codec_t::protobuf:
    return _state->protobuf_enabled;
  }
  return false;
}

send_call_t::send_call_t () = default;
send_call_t::send_call_t (std::shared_ptr<detail::connector_state_t> state,
                          packet_t packet)
  : _state (std::move (state)), _packet (std::move (packet))
{
}
send_call_t::~send_call_t () = default;
send_call_t::send_call_t (send_call_t &&) noexcept = default;
send_call_t &send_call_t::operator= (send_call_t &&) noexcept = default;

send_call_t &
send_call_t::packet_name (std::string name)
{
  _packet.name = std::move (name);
  return *this;
}

send_call_t &
send_call_t::metadata (std::string key, std::string value)
{
  _packet.metadata.with (std::move (key), std::move (value));
  return *this;
}

send_call_t &
send_call_t::metadata (metadata_t metadata)
{
  _packet.metadata = std::move (metadata);
  return *this;
}

send_call_t &
send_call_t::compress ()
{
  _packet.compressed = true;
  return *this;
}

task_t<void>
send_call_t::submit ()
{
  if (!_state) {
    return task_t<void> (result_t<void>::failure (
      error_code_t::configuration_error, "send call has no connector"));
  }
  return task_t<void> (detail::submit_send (_state, std::move (_packet)));
}

void
send_call_t::submit (std::function<void (result_t<void>)> callback)
{
  auto result = submit ().result ();
  if (callback) {
    callback (std::move (result));
  }
}

connector_t::connector_t ()
  : connector_t (connector_options_t {})
{
}

connector_t::connector_t (connector_options_t options)
  : _state (std::make_shared<detail::connector_state_t> (std::move (options))),
    _codecs (_state)
{
#ifndef ZLINK_STREAM_CONNECTOR_WITH_MESSAGEPACK
  _state->message_pack_enabled = false;
#else
  _state->message_pack_enabled = true;
#endif
#ifndef ZLINK_STREAM_CONNECTOR_WITH_PROTOBUF
  _state->protobuf_enabled = false;
#else
  _state->protobuf_enabled = true;
#endif
#ifndef ZLINK_STREAM_CONNECTOR_WITH_LZ4
  _state->lz4_enabled = false;
#else
  _state->lz4_enabled = true;
#endif
}

connector_t::~connector_t () = default;
connector_t::connector_t (connector_t &&) noexcept = default;
connector_t &connector_t::operator= (connector_t &&) noexcept = default;

bool
connector_t::is_connected () const
{
  return _state->state == connection_state_t::connected;
}

connection_state_t
connector_t::state () const
{
  return _state->state;
}

connector_options_t
connector_t::options () const
{
  return _state->options;
}

std::size_t
connector_t::pending_dispatch_count () const
{
  return _state->dispatch_queue.size ();
}

codec_registry_t &
connector_t::codecs ()
{
  return _codecs;
}

task_t<void>
connector_t::connect ()
{
  if (_state->options.endpoint.empty ()) {
    return task_t<void> (result_t<void>::failure (
      error_code_t::configuration_error,
      "stream connector endpoint is required"));
  }
  detail::change_state (_state, connection_state_t::connecting);
  detail::change_state (_state, connection_state_t::connected);
  return task_t<void> (result_t<void>::success ());
}

task_t<void>
connector_t::close ()
{
  detail::change_state (_state, connection_state_t::closed);
  _state->pending_requests.clear ();
  _state->dispatch_queue.clear ();
  return task_t<void> (result_t<void>::success ());
}

task_t<void>
connector_t::dispatch ()
{
  return task_t<void> (detail::dispatch_pending (_state));
}

connector_t &
connector_t::on_connection_state_changed (
  std::function<void (const connection_state_changed_t &)> handler)
{
  _state->state_handlers.push_back (std::move (handler));
  return *this;
}

connector_t &
connector_t::on_error (std::function<void (const error_t &)> handler)
{
  _state->error_handlers.push_back (std::move (handler));
  return *this;
}

connector_t &
connector_t::on_disconnected (std::function<void ()> handler)
{
  _state->disconnected_handlers.push_back (std::move (handler));
  return *this;
}

packet_t
connector_t::make_packet (std::type_index type) const
{
  packet_t packet;
  packet.name = type.name ();
  const auto codec = _state->codecs.find (type);
  packet.codec = codec == _state->codecs.end () ? codec_t::raw : codec->second;
  packet.payload = zlink::message_t::from (std::string ("typed"));
  return packet;
}

connector_t &
connector_t::on_packet_erased (
  std::string packet_name,
  std::function<void (const packet_t &)> handler)
{
  _state->packet_handlers[std::move (packet_name)].push_back (
    std::move (handler));
  return *this;
}

connector_t
connector_factory_t::create (connector_options_t options)
{
  return connector_t (std::move (options));
}

} // namespace zlink::stream_connector
