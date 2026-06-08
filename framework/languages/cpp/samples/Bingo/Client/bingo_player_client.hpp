/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "bingo_client_options.hpp"
#include "bingo_client_result.hpp"
#include "bingo_notification_inbox.hpp"
#include "../Shared/Contracts/messages.hpp"
#include "../../Shared/client_connector_helpers.hpp"

#include <zlink/stream_connector.hpp>

#include <string>
#include <utility>
#include <vector>

namespace zlink::samples::bingo
{

class bingo_player_client_t
{
  public:
    static bingo_player_client_t connect (std::string actor_id,
                                          const bingo_client_options_t &options)
    {
        auto connector =
          stream_connector::connector_factory_t::create (make_immediate_connector_options (
            options.stream_endpoint, options.connect_timeout, options.request_timeout));

        auto client = bingo_player_client_t (std::move (actor_id), std::move (connector));

        client.register_notifications ();
        client._connected = connect_client_connector (client._connector);
        return client;
    }

    const std::string &actor_id () const noexcept { return _actor_id; }
    bool connected () const noexcept { return _connected; }
    const bingo_notification_inbox_t &notifications () const noexcept { return _notifications; }

    bingo_client_call_result_t authenticate ()
    {
        return request<authenticate_res_t> (authenticate_req_t{_actor_id});
    }

    bingo_client_call_result_t match ()
    {
        return request<match_bingo_res_t> (match_bingo_req_t{"two-player"});
    }

    bingo_client_call_result_t submit_card (std::string room_id, std::vector<int> card)
    {
        return request<submit_bingo_card_res_t> (
          submit_bingo_card_req_t{std::move (room_id), std::move (card)});
    }

    void close () { zlink::samples::close_client_connector (_connector); }
    void dispatch () { zlink::samples::dispatch_client_connector (_connector); }

  private:
    bingo_player_client_t (std::string actor_id, zlink::stream_connector::connector_t connector) :
        _actor_id (std::move (actor_id)), _connector (std::move (connector))
    {
    }

    void register_notifications ()
    {
        zlink::stream_connector::codecs::on<player_joined_notify_t> (
          _connector,
          [this] (const player_joined_notify_t &message) { _notifications.on_joined (message); });
        zlink::stream_connector::codecs::on<game_started_notify_t> (
          _connector,
          [this] (const game_started_notify_t &message) { _notifications.on_started (message); });
        zlink::stream_connector::codecs::on<number_drawn_notify_t> (
          _connector,
          [this] (const number_drawn_notify_t &message) { _notifications.on_drawn (message); });
        zlink::stream_connector::codecs::on<game_ended_notify_t> (
          _connector,
          [this] (const game_ended_notify_t &message) { _notifications.on_ended (message); });
    }

    template <typename TReply, typename TRequest>
    bingo_client_call_result_t request (const TRequest &request_message)
    {
        return zlink::samples::request_client_packet<bingo_client_call_result_t, TReply> (
          _connector, request_message);
    }

    std::string _actor_id;
    zlink::stream_connector::connector_t _connector;
    bingo_notification_inbox_t _notifications;
    bool _connected = false;
};

} // namespace zlink::samples::bingo
