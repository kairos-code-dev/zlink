/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Handlers/ensure_player_actor_handler.hpp"
#include "../../../../../Configuration/sample_topology.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::tictactoe
{

using namespace framework;
using framework::actor_ref_t;
using framework::message_t;

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
        auto authenticated =
          co_await _client.request (sample_names_t::api_channel, authenticate_request)
            .async<authenticate_player_res_t> ();
        if (!authenticated.accepted || authenticated.player.actor_id.empty ()) {
            co_return result_t<session_actor_t>::failure (framework_error_kind_t::request_failed,
                                                          authenticated.reason.empty ()
                                                            ? "Player authentication failed."
                                                            : authenticated.reason);
        }

        const auto create_request = ensure_player_actor_req_t{authenticated.player.actor_id};
        const auto ensured = _ensure_actor.handle (create_request);
        auto bound = co_await actors.bind_or_get (ensured.actor.to_actor_ref (ensured.actor_type)).async ();
        auto joined =
          co_await bound.context ()
            .join_entry_spot (node_rid_t::from_string (_topology.selected_play_node_rid ()),
                              create_request)
            .async ();
        if (joined.result_code != 0) {
            co_return result_t<session_actor_t>::failure (framework_error_kind_t::request_failed,
                                                          "Player entry spot join was rejected.");
        }
        auto actor = actors.find (ensured.actor.actor_id).value_or (bound);

        const auto reply_payload = authenticate_res_t{authenticated.player};
        const auto reply_message = zlink::message_t::from_json (reply_payload);
        stream.reply_packet (reply_message).submit ();

        co_return actor;
    }

  private:
    channel_client_t &_client;
    ensure_player_actor_handler_t &_ensure_actor;
    sample_topology_t &_topology;
};

} // namespace zlink::samples::tictactoe
