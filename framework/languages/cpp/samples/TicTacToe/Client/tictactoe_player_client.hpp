/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "session_actor_notification_inbox.hpp"
#include "tictactoe_client_options.hpp"
#include "tictactoe_client_result.hpp"
#include "../Shared/Configuration/sample_names.hpp"
#include "../Shared/Contracts/messages.hpp"
#include "../../Shared/client_connector_helpers.hpp"

#include <zlink/stream_connector.hpp>

#include <string>
#include <utility>

namespace zlink::samples::tictactoe
{

class tictactoe_player_client_t
{
public:
  static tictactoe_player_client_t connect (
    std::string actor_id,
    const tictactoe_client_options_t &options)
  {
    auto connector =
      zlink::stream_connector::connector_factory_t::create (
        zlink::samples::make_immediate_connector_options (
          options.play_endpoint,
          options.stream_timeout,
          options.stream_timeout));
    auto client =
      tictactoe_player_client_t (std::move (actor_id), std::move (connector));
    client.register_notifications ();
    client._connected =
      zlink::samples::connect_client_connector (client._connector);
    return client;
  }

  bool connected () const noexcept { return _connected; }
  const notification_inbox_t &notifications () const noexcept
  {
    return _notifications;
  }

  tictactoe_client_call_result_t authenticate ()
  {
    return request<authenticate_res_t> (
      authenticate_req_t { _actor_id });
  }

  tictactoe_client_call_result_t join_game (std::string match_id)
  {
    return request<join_match_res_t> (
      join_match_req_t { std::move (match_id), _actor_id });
  }

  tictactoe_client_call_result_t create_match ()
  {
    return request<create_match_res_t> (
      create_match_req_t { _actor_id });
  }

  tictactoe_client_call_result_t place_mark (std::string match_id, int cell)
  {
    return request<place_mark_res_t> (
      place_mark_req_t { std::move (match_id), _actor_id, cell });
  }

  void close () { zlink::samples::close_client_connector (_connector); }
  void dispatch () { zlink::samples::dispatch_client_connector (_connector); }

private:
  tictactoe_player_client_t (
    std::string actor_id,
    zlink::stream_connector::connector_t connector)
    : _actor_id (std::move (actor_id)), _connector (std::move (connector))
  {
  }

  void register_notifications ()
  {
    zlink::stream_connector::codecs::on<opponent_joined_notify_t> (
      _connector,
      [this](const opponent_joined_notify_t &message) {
        _notifications.opponent_joined (message);
      });
    zlink::stream_connector::codecs::on<turn_changed_notify_t> (
      _connector,
      [this](const turn_changed_notify_t &message) {
        _notifications.turn_changed (message);
      });
    zlink::stream_connector::codecs::on<game_ended_notify_t> (
      _connector,
      [this](const game_ended_notify_t &message) {
        _notifications.game_ended (message);
      });
  }

  template<typename TReply, typename TRequest>
  tictactoe_client_call_result_t request (const TRequest &request_message)
  {
    return zlink::samples::request_client_packet<
      tictactoe_client_call_result_t,
      TReply> (_connector, request_message);
  }

  std::string _actor_id;
  zlink::stream_connector::connector_t _connector;
  notification_inbox_t _notifications;
  bool _connected = false;
};

} // namespace zlink::samples::tictactoe
