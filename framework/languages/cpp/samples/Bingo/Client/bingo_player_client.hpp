/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "bingo_client_options.hpp"
#include "bingo_client_result.hpp"
#include "bingo_notification_inbox.hpp"
#include "../Shared/Configuration/sample_names.hpp"
#include "../Shared/Contracts/messages.hpp"

#include <zlink/stream_connector.hpp>

#include <string>
#include <utility>

namespace zlink::samples::bingo
{

class bingo_player_client_t
{
public:
  static bingo_player_client_t connect (std::string actor_id,
                                        const bingo_client_options_t &options)
  {
    zlink::stream_connector::connector_options_t connector_options;
    connector_options.endpoint = options.stream_endpoint;
    connector_options.connect_timeout = options.connect_timeout;
    connector_options.request_timeout = options.request_timeout;
    connector_options.dispatch_mode =
      zlink::stream_connector::dispatch_mode_t::immediate;

    auto connector =
      zlink::stream_connector::connector_factory_t::create (connector_options);
    auto client =
      bingo_player_client_t (std::move (actor_id), std::move (connector));
    client.register_notifications ();
    client._connected = static_cast<bool> (client._connector.connect ().result ());
    return client;
  }

  const std::string &actor_id () const noexcept { return _actor_id; }
  bool connected () const noexcept { return _connected; }
  const bingo_notification_inbox_t &notifications () const noexcept
  {
    return _notifications;
  }

  bingo_client_call_result_t authenticate ()
  {
    return request<authenticate_res_t> (
      authenticate_req_t { _actor_id });
  }

  bingo_client_call_result_t match ()
  {
    return request<match_bingo_res_t> (
      match_bingo_req_t { "four-player" });
  }

  bingo_client_call_result_t start (std::string room_id)
  {
    return request<start_bingo_game_res_t> (
      start_bingo_game_req_t { std::move (room_id) });
  }

  bingo_client_call_result_t leave (std::string room_id)
  {
    auto result =
      _connector.send (leave_room_req_t { std::move (room_id) })
        .submit ()
        .result ();
    return { leave_room_req_t::packet_name,
             static_cast<bool> (result),
             result ? std::string {}
                    : result.error () ? result.error ()->message
                                      : "send failed" };
  }

  void close () { _connector.close ().result (); }
  void dispatch () { _connector.dispatch ().result (); }

private:
  bingo_player_client_t (std::string actor_id,
                         zlink::stream_connector::connector_t connector)
    : _actor_id (std::move (actor_id)), _connector (std::move (connector))
  {
  }

  void register_notifications ()
  {
    _connector.on<player_joined_notify_t> (
      [this](const player_joined_notify_t &message) {
        _notifications.on_joined (message);
      });
    _connector.on<game_started_notify_t> (
      [this](const game_started_notify_t &message) {
        _notifications.on_started (message);
      });
    _connector.on<number_drawn_notify_t> (
      [this](const number_drawn_notify_t &message) {
        _notifications.on_drawn (message);
      });
    _connector.on<game_ended_notify_t> (
      [this](const game_ended_notify_t &message) {
        _notifications.on_ended (message);
      });
  }

  template<typename TReply, typename TRequest>
  bingo_client_call_result_t request (const TRequest &request_message)
  {
    auto result =
      _connector.request<TReply> (request_message)
        .submit ()
        .result ();
    return { TRequest::packet_name,
             static_cast<bool> (result),
             result ? std::string {}
                    : result.error () ? result.error ()->message
                                      : "request failed" };
  }

  std::string _actor_id;
  zlink::stream_connector::connector_t _connector;
  bingo_notification_inbox_t _notifications;
  bool _connected = false;
};

} // namespace zlink::samples::bingo
