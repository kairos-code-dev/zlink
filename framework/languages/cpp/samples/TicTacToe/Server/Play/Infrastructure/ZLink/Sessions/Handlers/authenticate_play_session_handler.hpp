/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Handlers/ensure_player_actor_handler.hpp"
#include "../../../../../Configuration/sample_topology.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::tictactoe
{

class authenticate_play_session_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::channel_client_t,
                                          ensure_player_actor_handler_t,
                                          sample_topology_t>;

    authenticate_play_session_handler_t (zlink::framework::channel_client_t &client,
                                         ensure_player_actor_handler_t &ensure_actor,
                                         sample_topology_t &topology) :
        _client (client), _ensure_actor (ensure_actor), _topology (topology)
    {
    }

    bool can_handle (const zlink::framework::stream_dispatch_context_t &dispatch) const
    {
        return dispatch.packet_name () == authenticate_req_t::packet_name;
    }

    zlink::framework::task_t<zlink::framework::session_actor_t>
    handle (zlink::framework::session_actor_manager_t &actors,
            zlink::framework::stream_t &stream,
            const zlink::message_t &payload)
    {
        auto request = payload.parse_json<authenticate_req_t> ();
        auto authenticated = co_await _client
                               .request (
                                 sample_names_t::api_channel,
                                 authenticate_player_req_t{request.access_token})
                               .async<authenticate_player_res_t> ();
        if (!authenticated.accepted || authenticated.player.actor_id.empty ()) {
            co_return zlink::framework::result_t<zlink::framework::session_actor_t>::failure (
              zlink::framework::framework_error_kind_t::request_failed,
              authenticated.reason.empty () ? "Player authentication failed."
                                            : authenticated.reason);
        }

        const auto create_request = ensure_player_actor_req_t{authenticated.player.actor_id};
        const auto ensured = _ensure_actor.handle (create_request);
        auto bound = co_await actors.bind (to_actor_ref (ensured)).async ();
        auto joined = co_await bound.context ()
                        .join_entry_spot (
                          zlink::framework::node_rid_t::from_string (
                            _topology.selected_play_node_rid ()),
                          create_request)
                        .async ();
        if (joined.result_code != 0) {
            co_return zlink::framework::result_t<zlink::framework::session_actor_t>::failure (
              zlink::framework::framework_error_kind_t::request_failed,
              "Player entry spot join was rejected.");
        }
        auto actor = actors.find (ensured.actor.actor_id).value_or (bound);

        co_await stream
          .reply_packet (zlink::message_t::from_json (
            authenticate_res_t{authenticated.player}))
          .async ();

        co_return actor;
    }

  private:
    zlink::framework::actor_ref_t to_actor_ref (const ensure_player_actor_res_t &ensured) const
    {
        return zlink::framework::actor_ref_t (
          zlink::framework::node_rid_t::from_string (_topology.selected_play_node_rid ()), ensured.actor_type,
          ensured.actor.actor_id, ensured.actor.generation);
    }

    zlink::framework::channel_client_t &_client;
    ensure_player_actor_handler_t &_ensure_actor;
    sample_topology_t &_topology;
};

} // namespace zlink::samples::tictactoe
