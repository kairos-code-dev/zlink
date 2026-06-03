/* SPDX-License-Identifier: MPL-2.0 */

#include "connector_runtime.hpp"

#include "runtime/protocol/framing.hpp"
#include "runtime/protocol/packet_name_resolver.hpp"
#include "runtime/transport/stream_connection.hpp"
#include "runtime/transport/stream_transport_factory.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <utility>

namespace zlink::stream_connector::detail
{

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
deliver_received_packet (connector_state_t &state, packet_t packet)
{
  if (packet.name.rfind ("$zlink.", 0) == 0) {
    return;
  }
  if (state.options.dispatch_mode == dispatch_mode_t::immediate) {
    dispatch_packet (state, packet);
    return;
  }
  state.dispatch_queue.push_back (std::move (packet));
}

void
connector_runtime_t::receive_packet (packet_t packet)
{
  deliver_received_packet (*_state, std::move (packet));
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
send_call_t::codec (codec_t codec)
{
  _packet.codec = codec;
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
  auto task = submit ();
  task.on_completed (std::move (callback));
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
  if (!detail::stream_transport_factory_t::is_supported (
        _state->options.transport)) {
    return task_t<void> (result_t<void>::failure (
      error_code_t::configuration_error,
      "stream connector currently supports the tcp transport only"));
  }
  const auto parsed = detail::parse_tcp_endpoint (_state->options.endpoint);
  if (!parsed) {
    return task_t<void> (result_t<void>::failure (
      error_code_t::configuration_error,
      "stream connector endpoint must use tcp://host:port"));
  }

  const auto max_attempts =
    _state->options.reconnect.enabled
      ? std::max (1, _state->options.reconnect.max_attempts.value_or (1))
      : 1;
  auto retry_delay = _state->options.reconnect.initial_delay;
  std::string last_error;

  for (int attempt = 1; attempt <= max_attempts; ++attempt) {
    detail::change_state (
      _state,
      attempt == 1 ? connection_state_t::connecting
                   : connection_state_t::reconnecting);
    try {
      boost::asio::ip::tcp::resolver resolver (_state->io_context);
      auto endpoints = resolver.resolve (parsed->host, parsed->port);
      boost::asio::ip::tcp::socket socket (_state->io_context);
      boost::asio::connect (socket, endpoints);
      _state->connection = detail::make_tcp_connection (std::move (socket));
      const auto now = std::chrono::steady_clock::now ();
      _state->last_heartbeat_sent = now;
      _state->last_inbound_received = now;
      detail::change_state (_state, connection_state_t::connected);
      return task_t<void> (result_t<void>::success ());
    } catch (const std::exception &ex) {
      last_error = ex.what ();
      boost::system::error_code ignored;
      if (_state->connection) {
        _state->connection->close (ignored);
      }
      if (attempt < max_attempts) {
        std::this_thread::sleep_for (retry_delay);
        retry_delay = std::min (
          _state->options.reconnect.max_delay,
          std::chrono::milliseconds (static_cast<int> (
            retry_delay.count () * _state->options.reconnect.backoff_factor)));
      }
    }
  }

  detail::change_state (
    _state,
    connection_state_t::disconnected,
    error_t { error_code_t::connect_timeout, last_error });
  return task_t<void> (
    result_t<void>::failure (error_code_t::connect_timeout, last_error));
}

task_t<void>
connector_t::close ()
{
  if (_state->connection && _state->connection->is_open ()) {
    _state->connection->shutdown_and_close ();
  }
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
connector_t::make_packet (std::type_index type, std::string packet_name) const
{
  packet_t packet;
  packet.name =
    detail::packet_name_resolver_t {}.resolve (type, std::move (packet_name));
  const auto codec = _state->codecs.find (type);
  packet.codec = codec == _state->codecs.end () ? codec_t::raw : codec->second;
  packet.payload = zlink::message_t::from (std::string ("{}"));
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
