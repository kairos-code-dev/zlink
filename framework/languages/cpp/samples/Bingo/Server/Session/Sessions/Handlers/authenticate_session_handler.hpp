/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Configuration/sample_names.hpp"
#include "../../../Configuration/sample_topology.hpp"
#include "../../../../Shared/Contracts/messages.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::bingo
{

class authenticate_session_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::channel_client_t,
                                          zlink::framework::route_client_t,
                                          sample_topology_t>;

    explicit authenticate_session_handler_t (zlink::framework::channel_client_t &client,
                                             zlink::framework::route_client_t &routes,
                                             sample_topology_t &topology) :
        _client (client), _routes (routes), _topology (topology)
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
        if (!authenticated.accepted || authenticated.actor_id.empty ()
            || authenticated.display_name.empty ()) {
            co_return zlink::framework::result_t<zlink::framework::session_actor_t>::failure (
              zlink::framework::framework_error_kind_t::request_failed,
              authenticated.reason.empty () ? "Player authentication failed."
                                            : authenticated.reason);
        }

        auto ensured = co_await _routes
                         .request (
                           sample_names_t::play_channel,
                           zlink::routing_id_t::from (_topology.preferred_play_node_rid ()),
                           ensure_player_actor_req_t{authenticated.actor_id,
                                                     authenticated.display_name,
                                                     _topology.preferred_play_node_rid ()})
                         .async<ensure_player_actor_res_t> ();
        auto bound = co_await actors.bind (to_actor_ref (ensured)).async ();

        co_await stream
          .reply_packet (zlink::message_t::from_json (
            authenticate_res_t{ensured.actor_id, authenticated.display_name,
                               ensured.actor.node_rid}))
          .async ();

        co_return bound;
    }

  private:
    zlink::framework::actor_ref_t to_actor_ref (const ensure_player_actor_res_t &ensured) const
    {
        return zlink::framework::actor_ref_t (
          zlink::framework::node_rid_t::from_string (ensured.actor.node_rid),
          ensured.actor_type,
          ensured.actor.actor_id, ensured.actor.generation);
    }

    zlink::framework::channel_client_t &_client;
    zlink::framework::route_client_t &_routes;
    sample_topology_t &_topology;
};

} // namespace zlink::samples::bingo
