/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../Handlers/ensure_player_actor_handler.hpp"
#include "../../../../../Configuration/sample_topology.hpp"

#include <zlink/framework.hpp>

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace zlink::samples::tictactoe
{

using namespace framework;
using framework::actor_ref_t;
using framework::message_t;

inline bool tictactoe_auth_trace_enabled ()
{
    const char *value = std::getenv ("ZLINK_CPP_TICTACTOE_AUTH_TRACE");
    return value != nullptr && value[0] != '\0' && std::string_view (value) != "0";
}

inline void trace_tictactoe_auth (std::string_view stage, std::string_view detail = {})
{
    if (!tictactoe_auth_trace_enabled ()) {
        return;
    }
    std::cerr << "zlink-cpp-tictactoe-auth-trace side=play stage=" << stage;
    if (!detail.empty ()) {
        std::cerr << " " << detail;
    }
    std::cerr << std::endl;
}

class authenticate_play_session_handler_t
{
  public:
    using dependency_types =
      dependency_list_t<channel_client_t, ensure_player_actor_handler_t, sample_topology_t>;

    authenticate_play_session_handler_t (channel_client_t &client,
                                         ensure_player_actor_handler_t &ensure_actor,
                                         sample_topology_t &topology) :
        _client (client), _ensure_actor (ensure_actor), _topology (topology)
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
        trace_tictactoe_auth ("api-auth-request-start",
                               std::string_view (request.access_token));
        auto authenticated =
          co_await _client.request (sample_names_t::api_channel, authenticate_request)
            .async<authenticate_player_res_t> ();
        trace_tictactoe_auth ("api-auth-request-complete",
                               authenticated.accepted
                                 ? std::string_view (authenticated.player.actor_id)
                                 : std::string_view ("rejected"));
        if (!authenticated.accepted || authenticated.player.actor_id.empty ()) {
            co_return result_t<session_actor_t>::failure (framework_error_kind_t::request_failed,
                                                          authenticated.reason.empty ()
                                                            ? "Player authentication failed."
                                                            : authenticated.reason);
        }

        const auto create_request = ensure_player_actor_req_t{authenticated.player.actor_id};
        trace_tictactoe_auth ("ensure-actor-start", authenticated.player.actor_id);
        const auto ensured = _ensure_actor.handle (create_request);
        trace_tictactoe_auth ("ensure-actor-complete", ensured.actor.actor_id);
        trace_tictactoe_auth ("bind-actor-start", ensured.actor.actor_id);
        auto bound = co_await actors.bind_or_get (ensured.actor.to_actor_ref (ensured.actor_type)).async ();
        trace_tictactoe_auth ("bind-actor-complete", ensured.actor.actor_id);
        trace_tictactoe_auth ("join-entry-spot-start", ensured.actor.actor_id);
        auto joined =
          co_await bound.context ()
            .join_entry_spot (node_rid_t::from_string (_topology.selected_play_node_rid ()),
                              create_request)
            .async ();
        trace_tictactoe_auth ("join-entry-spot-complete", ensured.actor.actor_id);
        if (!std::holds_alternative<
              framework::actor_join_accepted_t<framework::message_t>> (joined)) {
            co_return result_t<session_actor_t>::failure (framework_error_kind_t::request_failed,
                                                          "Player entry spot join was rejected.");
        }
        auto actor = actors.find (ensured.actor.actor_id).value_or (bound);

        const auto reply_payload = authenticate_res_t{authenticated.player};
        const auto reply_message = zlink::message_t::from_json (reply_payload);
        trace_tictactoe_auth ("reply-submit", authenticated.player.actor_id);
        stream.reply_packet (reply_message).submit ();

        co_return actor;
    }

  private:
    channel_client_t &_client;
    ensure_player_actor_handler_t &_ensure_actor;
    sample_topology_t &_topology;
};

} // namespace zlink::samples::tictactoe
