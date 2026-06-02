/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/stream_connector/contracts/calls/zlink_stream_calls.hpp>
#include <zlink/stream_connector/contracts/codec_registry.hpp>
#include <zlink/stream_connector/contracts/stream_payload.hpp>
#include <zlink/stream_connector/contracts/zlink_stream_connector_options.hpp>

#include <functional>
#include <cstddef>
#include <memory>
#include <string>
#include <typeindex>
#include <utility>

namespace zlink::stream_connector
{

class connector_t
{
public:
  connector_t ();
  ~connector_t ();

  connector_t (connector_t &&) noexcept;
  connector_t &operator= (connector_t &&) noexcept;
  connector_t (const connector_t &) = default;
  connector_t &operator= (const connector_t &) = default;

  bool is_connected () const;
  connection_state_t state () const;
  connector_options_t options () const;
  std::size_t pending_dispatch_count () const;
  codec_registry_t &codecs ();

  task_t<void> connect ();
  task_t<void> close ();
  task_t<void> dispatch ();

  connector_t &on_connection_state_changed (
    std::function<void (const connection_state_changed_t &)> handler);
  connector_t &on_error (std::function<void (const error_t &)> handler);
  connector_t &on_disconnected (std::function<void ()> handler);

  template<typename TMessage>
  send_call_t send (const TMessage &message)
  {
    auto packet = make_packet<TMessage> ();
    packet.payload = detail::to_packet_payload (message, 0);
    return send_call_t (_state, std::move (packet));
  }

  send_call_t send (packet_t packet)
  {
    if (packet.name.empty ()) {
      packet.name = "packet";
    }
    return send_call_t (_state, std::move (packet));
  }

  template<typename TReply, typename TRequest>
  request_call_t<TReply> request (const TRequest &request)
  {
    auto packet = make_packet<TRequest> ();
    packet.payload = detail::to_packet_payload (request, 0);
    return request_call_t<TReply> (
      _state,
      std::move (packet),
      options ().request_timeout);
  }

  template<typename TReply>
  request_call_t<TReply> request (packet_t packet)
  {
    if (packet.name.empty ()) {
      packet.name = "packet";
    }
    return request_call_t<TReply> (
      _state,
      std::move (packet),
      options ().request_timeout);
  }

  template<typename TMessage>
  connector_t &on (std::string packet_name,
                   std::function<void (const TMessage &)> callback)
  {
    return on_packet_erased (
      std::move (packet_name),
      [callback = std::move (callback)](const packet_t &packet) {
        if constexpr (std::is_same_v<TMessage, packet_t>) {
          callback (packet);
        } else {
          TMessage message {};
          detail::apply_packet_payload (message, packet.payload, 0);
          callback (std::move (message));
        }
      });
  }

  template<typename TMessage>
  connector_t &on (std::function<void (const TMessage &)> callback)
  {
    return on<TMessage> (
      detail::message_packet_name<TMessage> (), std::move (callback));
  }

private:
  friend class connector_factory_t;
  friend class detail::connector_runtime_t;

  explicit connector_t (connector_options_t options);
  template<typename TMessage>
  packet_t make_packet () const
  {
    return make_packet (
      std::type_index (typeid (TMessage)),
      detail::message_packet_name<TMessage> ());
  }

  packet_t make_packet (std::type_index type, std::string packet_name) const;
  connector_t &on_packet_erased (
    std::string packet_name,
    std::function<void (const packet_t &)> handler);

  std::shared_ptr<detail::connector_state_t> _state;
  codec_registry_t _codecs;
};

} // namespace zlink::stream_connector
