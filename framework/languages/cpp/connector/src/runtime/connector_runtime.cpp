/* SPDX-License-Identifier: MPL-2.0 */

#include "connector_runtime.hpp"

#include "runtime/protocol/framing.hpp"
#include "runtime/protocol/packet_name_resolver.hpp"
#include "runtime/transport/stream_connection.hpp"
#include "runtime/transport/stream_transport_factory.hpp"
#include "runtime/transport/websocket_connection.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <utility>

namespace zlink::stream_connector::detail
{

std::shared_ptr<connector_state_t> state_from (const std::shared_ptr<void> &state)
{
    return std::static_pointer_cast<connector_state_t> (state);
}

connector_runtime_t::connector_runtime_t (std::shared_ptr<connector_state_t> state) :
    _state (std::move (state))
{
}

connector_runtime_t connector_runtime_t::from (const connector_t &connector)
{
    return connector_runtime_t (state_from (connector_internal_handle (connector)));
}

void deliver_received_packet (connector_state_t &state, packet_t packet)
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

void connector_runtime_t::receive_packet (packet_t packet)
{
    deliver_received_packet (*_state, std::move (packet));
}

const std::vector<packet_t> &connector_runtime_t::sent_packets () const noexcept
{
    return _state->sent_packets;
}

std::size_t connector_runtime_t::pending_request_count () const noexcept
{
    return _state->pending_requests.size ();
}

void change_state (std::shared_ptr<connector_state_t> state,
                   connection_state_t next,
                   std::optional<error_t> error)
{
    const auto previous = state->state;
    state->state = next;
    connection_state_changed_t changed{previous, next, error};
    for (const auto &handler : state->state_handlers) {
        handler (changed);
    }
    if (next == connection_state_t::disconnected || next == connection_state_t::closed) {
        for (const auto &handler : state->disconnected_handlers) {
            handler ();
        }
    }
}

} // namespace zlink::stream_connector::detail

namespace zlink::stream_connector
{

std::shared_ptr<void> connector_internal_handle (const connector_t &connector)
{
    return connector._state;
}

codec_registry_t::codec_registry_t () :
    _state (std::make_shared<detail::connector_state_t> (connector_options_t{}))
{
}

codec_registry_t::codec_registry_t (std::shared_ptr<void> state) : _state (std::move (state))
{
}

codec_registry_t::~codec_registry_t () = default;
codec_registry_t::codec_registry_t (codec_registry_t &&) noexcept = default;
codec_registry_t &codec_registry_t::operator= (codec_registry_t &&) noexcept = default;

codec_registry_t &codec_registry_t::add_erased (std::type_index type, codec_t codec)
{
    if (!supports (codec)) {
        throw std::invalid_argument ("stream connector codec is not enabled");
    }
    auto state = detail::state_from (_state);
    state->codecs[type] = codec;
    return *this;
}

bool codec_registry_t::supports (codec_t codec) const
{
    auto state = detail::state_from (_state);
    switch (codec) {
        case codec_t::raw:
            return true;
        case codec_t::json:
            return state->json_enabled;
        case codec_t::message_pack:
            return state->message_pack_enabled;
        case codec_t::protobuf:
            return state->protobuf_enabled;
    }
    return false;
}

send_call_t::send_call_t () = default;
send_call_t::send_call_t (std::shared_ptr<void> state, packet_t packet) :
    _state (std::move (state)), _packet (std::move (packet))
{
}
send_call_t::~send_call_t () = default;
send_call_t::send_call_t (send_call_t &&) noexcept = default;
send_call_t &send_call_t::operator= (send_call_t &&) noexcept = default;

send_call_t &send_call_t::packet_name (std::string name)
{
    _packet.name = std::move (name);
    return *this;
}

send_call_t &send_call_t::metadata (std::string key, std::string value)
{
    _packet.metadata.with (std::move (key), std::move (value));
    return *this;
}

send_call_t &send_call_t::metadata (metadata_t metadata)
{
    _packet.metadata = std::move (metadata);
    return *this;
}

send_call_t &send_call_t::codec (codec_t codec)
{
    _packet.codec = codec;
    return *this;
}

send_call_t &send_call_t::compress ()
{
    _packet.compressed = true;
    return *this;
}

result_t<void> send_call_t::submit ()
{
    if (!_state) {
        return result_t<void>::failure (error_code_t::configuration_error,
                                        "send call has no connector");
    }
    return detail::submit_send (detail::state_from (_state), std::move (_packet));
}

task_t<void> send_call_t::async ()
{
    return task_t<void> (submit ());
}

void send_call_t::submit (std::function<void (result_t<void>)> callback)
{
    auto task = async ();
    task.on_completed (std::move (callback));
}

connector_t::connector_t () : connector_t (connector_options_t{})
{
}

connector_t::connector_t (connector_options_t options) :
    _state (std::make_shared<detail::connector_state_t> (std::move (options))), _codecs (_state)
{
    auto state = detail::state_from (_state);
    state->message_pack_enabled = true;
    state->protobuf_enabled = true;
#ifndef ZLINK_STREAM_CONNECTOR_WITH_LZ4
    state->lz4_enabled = false;
#else
    state->lz4_enabled = true;
#endif
}

connector_t::~connector_t () = default;
connector_t::connector_t (connector_t &&) noexcept = default;
connector_t &connector_t::operator= (connector_t &&) noexcept = default;

bool connector_t::is_connected () const
{
    return detail::state_from (_state)->state == connection_state_t::connected;
}

connection_state_t connector_t::state () const
{
    return detail::state_from (_state)->state;
}

connector_options_t connector_t::options () const
{
    return detail::state_from (_state)->options;
}

std::size_t connector_t::pending_dispatch_count () const
{
    return detail::state_from (_state)->dispatch_queue.size ();
}

codec_registry_t &connector_t::codecs ()
{
    return _codecs;
}

result_t<void> connector_t::connect ()
{
    auto state = detail::state_from (_state);
    if (state->options.endpoint.empty ()) {
        return result_t<void>::failure (error_code_t::configuration_error,
                                        "stream connector endpoint is required");
    }
    if (!detail::stream_transport_factory_t::is_supported (state->options.transport)) {
        return result_t<void>::failure (
          error_code_t::configuration_error,
          "stream connector does not support the configured transport in this build");
    }
    const auto tcp_endpoint = state->options.transport == transport_t::tcp
                                ? detail::parse_tcp_endpoint (state->options.endpoint)
                                : std::optional<detail::endpoint_parts_t>{};
    const auto tls_endpoint = state->options.transport == transport_t::tls
                                ? detail::parse_tls_endpoint (state->options.endpoint)
                                : std::optional<detail::endpoint_parts_t>{};
    const auto websocket_endpoint = state->options.transport == transport_t::websocket
                                      ? detail::parse_websocket_endpoint (state->options.endpoint)
                                      : std::optional<detail::websocket_endpoint_parts_t>{};
    const auto websocket_secure_endpoint =
      state->options.transport == transport_t::websocket_secure
        ? detail::parse_websocket_secure_endpoint (state->options.endpoint)
        : std::optional<detail::websocket_endpoint_parts_t>{};
    if (state->options.transport == transport_t::tcp && !tcp_endpoint) {
        return result_t<void>::failure (error_code_t::configuration_error,
                                        "stream connector endpoint must use tcp://host:port");
    }
    if (state->options.transport == transport_t::tls && !tls_endpoint) {
        return result_t<void>::failure (error_code_t::configuration_error,
                                        "stream connector endpoint must use tls://host:port");
    }
    if (state->options.transport == transport_t::websocket && !websocket_endpoint) {
        return result_t<void>::failure (error_code_t::configuration_error,
                                        "stream connector endpoint must use ws://host:port/path");
    }
    if (state->options.transport == transport_t::websocket_secure && !websocket_secure_endpoint) {
        return result_t<void>::failure (error_code_t::configuration_error,
                                        "stream connector endpoint must use wss://host:port/path");
    }

    const auto max_attempts = state->options.reconnect.enabled
                                ? std::max (1, state->options.reconnect.max_attempts.value_or (1))
                                : 1;
    auto retry_delay = state->options.reconnect.initial_delay;
    std::string last_error;

    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        detail::change_state (state, attempt == 1 ? connection_state_t::connecting
                                                  : connection_state_t::reconnecting);
        try {
            if (state->options.transport == transport_t::websocket) {
                state->connection =
                  detail::connect_websocket (state->io_context, *websocket_endpoint);
            } else if (state->options.transport == transport_t::websocket_secure) {
#ifdef ZLINK_STREAM_CONNECTOR_WITH_OPENSSL
                state->connection = detail::connect_websocket_secure (
                  state->io_context, *websocket_secure_endpoint,
                  state->options.skip_server_certificate_validation);
#else
                throw std::runtime_error ("stream connector WSS support requires OpenSSL");
#endif
            } else if (state->options.transport == transport_t::tls) {
#ifdef ZLINK_STREAM_CONNECTOR_WITH_OPENSSL
                state->connection =
                  detail::connect_tls (state->io_context, *tls_endpoint,
                                       state->options.skip_server_certificate_validation);
#else
                throw std::runtime_error ("stream connector TLS support requires OpenSSL");
#endif
            } else {
                boost::asio::ip::tcp::resolver resolver (state->io_context);
                auto endpoints = resolver.resolve (tcp_endpoint->host, tcp_endpoint->port);
                boost::asio::ip::tcp::socket socket (state->io_context);
                boost::asio::connect (socket, endpoints);
                state->connection = detail::make_tcp_connection (std::move (socket));
            }
            const auto now = std::chrono::steady_clock::now ();
            state->last_heartbeat_sent = now;
            state->last_inbound_received = now;
            detail::change_state (state, connection_state_t::connected);
            return result_t<void>::success ();
        }
        catch (const std::exception &ex) {
            last_error = ex.what ();
            boost::system::error_code ignored;
            if (state->connection) {
                state->connection->close (ignored);
            }
            if (attempt < max_attempts) {
                std::this_thread::sleep_for (retry_delay);
                retry_delay =
                  std::min (state->options.reconnect.max_delay,
                            std::chrono::milliseconds (static_cast<int> (
                              retry_delay.count () * state->options.reconnect.backoff_factor)));
            }
        }
    }

    detail::change_state (state, connection_state_t::disconnected,
                          error_t{error_code_t::connect_timeout, last_error});
    return result_t<void>::failure (error_code_t::connect_timeout, last_error);
}

result_t<void> connector_t::close ()
{
    auto state = detail::state_from (_state);
    if (state->connection && state->connection->is_open ()) {
        state->connection->shutdown_and_close ();
    }
    detail::change_state (state, connection_state_t::closed);
    state->pending_requests.clear ();
    state->dispatch_queue.clear ();
    return result_t<void>::success ();
}

result_t<void> connector_t::dispatch ()
{
    return detail::dispatch_pending (detail::state_from (_state));
}

result_t<packet_t> connector_t::wait_for (std::string packet_name,
                                          std::chrono::milliseconds timeout)
{
    return detail::wait_for_packet (
      detail::state_from (_state), std::move (packet_name), nullptr, timeout);
}

connector_t &connector_t::on_connection_state_changed (
  std::function<void (const connection_state_changed_t &)> handler)
{
    detail::state_from (_state)->state_handlers.push_back (std::move (handler));
    return *this;
}

connector_t &connector_t::on_error (std::function<void (const error_t &)> handler)
{
    detail::state_from (_state)->error_handlers.push_back (std::move (handler));
    return *this;
}

connector_t &connector_t::on_disconnected (std::function<void ()> handler)
{
    detail::state_from (_state)->disconnected_handlers.push_back (std::move (handler));
    return *this;
}

packet_t connector_t::make_packet (std::type_index type, std::string packet_name) const
{
    packet_t packet;
    packet.name = detail::packet_name_resolver_t{}.resolve (type, std::move (packet_name));
    const auto state = detail::state_from (_state);
    const auto codec = state->codecs.find (type);
    packet.codec = codec == state->codecs.end () ? codec_t::raw : codec->second;
    packet.payload = zlink::message_t::from (std::string ("{}"));
    return packet;
}

connector_t &connector_t::on_packet_erased (std::string packet_name,
                                            std::function<void (const packet_t &)> handler)
{
    detail::state_from (_state)->packet_handlers[std::move (packet_name)].push_back (
      std::move (handler));
    return *this;
}

connector_t connector_factory_t::create (connector_options_t options)
{
    return connector_t (std::move (options));
}

} // namespace zlink::stream_connector
