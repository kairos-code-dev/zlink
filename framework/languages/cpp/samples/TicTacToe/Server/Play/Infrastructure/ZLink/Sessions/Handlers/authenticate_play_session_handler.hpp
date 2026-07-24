/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../../../../Configuration/sample_names.hpp"
#include "../../../../../Configuration/sample_topology.hpp"
#include "../../../../../../Shared/Contracts/messages.hpp"

#include <zlink/framework.hpp>

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace zlink::samples::tictactoe
{

using namespace framework;
using framework::actor_ref_t;
using framework::message_t;

class authenticate_play_session_handler_t
{
  public:
    using dependency_types = dependency_list_t<channel_client_t, sample_topology_t>;

    authenticate_play_session_handler_t (channel_client_t &client, sample_topology_t &topology) :
        _client (client), _topology (topology)
    {
    }

    bool can_handle (const stream_dispatch_context_t &dispatch) const
    {
        return dispatch.packet_name () == authenticate_req_t::packet_name;
    }

    task_t<session_actor_t>
    handle (session_actor_manager_t &actors, stream_t &stream, const zlink::message_t &payload)
    {
        auto request = payload.parse_json<authenticate_req_t> ();
        const auto authenticate_request = authenticate_player_req_t{request.access_token};
        auto authenticated =
          co_await _client.request (sample_names_t::api_channel, authenticate_request)
            .submit<authenticate_player_res_t> ();
        if (!authenticated.accepted || authenticated.player.actor_id.empty ()) {
            co_return result_t<session_actor_t>::failure (framework_error_kind_t::request_failed,
                                                          authenticated.reason.empty ()
                                                            ? "Player authentication failed."
                                                            : authenticated.reason);
        }

        /* 공통 sample spec §13: 인증 응답의 PlayerInfo.ActorId로 actor를 만들고, 같은
         * PlayerInfo를 actor 생성 payload로 실어 보낸다(별도 EnsurePlayerActor 계약 없음). */
        const auto &player = authenticated.player;
        const auto play_node_rid = node_rid_t::from_string (_topology.selected_play_node_rid ());
        const actor_ref_t actor_ref (play_node_rid, sample_names_t::actor_type, player.actor_id,
                                     ++_generation);
        auto bound = co_await actors.bind_or_get (actor_ref).submit ();
        auto joined = co_await bound.context ().join_entry_spot (play_node_rid, player).async ();
        if (!std::holds_alternative<
              framework::actor_join_accepted_t<framework::message_t>> (joined)) {
            co_return result_t<session_actor_t>::failure (framework_error_kind_t::request_failed,
                                                          "Player entry spot join was rejected.");
        }
        auto actor = actors.find (player.actor_id).value_or (bound);

        const auto reply_payload = authenticate_res_t{player};
        const auto reply_message = zlink::message_t::from_json (reply_payload);
        stream.reply_packet (reply_message).submit ();

        co_return actor;
    }

  private:
    channel_client_t &_client;
    sample_topology_t &_topology;
    unsigned long long _generation = 0;
};

} // namespace zlink::samples::tictactoe
