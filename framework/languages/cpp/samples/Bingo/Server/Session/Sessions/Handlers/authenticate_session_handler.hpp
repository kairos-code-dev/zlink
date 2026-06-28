/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Configuration/sample_names.hpp"
#include "../../../Configuration/sample_topology.hpp"
#include "../../../../Shared/Contracts/messages.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::bingo
{

using namespace framework;
using framework::actor_ref_t;
using framework::message_t;

class authenticate_session_handler_t
{
  public:
    using dependency_types = dependency_list_t<channel_client_t, route_client_t, sample_topology_t>;

    explicit authenticate_session_handler_t (channel_client_t &client,
                                             route_client_t &routes,
                                             sample_topology_t &topology) :
        _client (client), _routes (routes), _topology (topology)
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
        auto authenticated = co_await _client.request (
            sample_names_t::api_channel, authenticate_request).async<authenticate_player_res_t> ();
        if (!authenticated.accepted || authenticated.actor_id.empty ()
            || authenticated.display_name.empty ()) {
            co_return result_t<session_actor_t>::failure (framework_error_kind_t::request_failed,
                                                          authenticated.reason.empty ()
                                                            ? "Player authentication failed."
                                                            : authenticated.reason);
        }

        auto create_request = ensure_player_actor_req_t{
            authenticated.actor_id, authenticated.display_name, _topology.preferred_play_node_rid ()
        };
        auto ensured = co_await _routes
            .request (sample_names_t::play_channel,
                      zlink::routing_id_t::from (_topology.preferred_play_node_rid ()),
                      create_request).async<ensure_player_actor_res_t> ();
        auto bound = co_await actors.bind (to_actor_ref (ensured)).async ();
        auto joined = co_await bound.context ()
            .join_entry_spot (node_rid_t::from_string (ensured.actor.node_rid), create_request)
            .async ();
        if (joined.result_code != 0) {
            co_return result_t<session_actor_t>::failure (framework_error_kind_t::request_failed,
                                                          "Player entry spot join was rejected.");
        }
        auto actor = actors.find (ensured.actor.actor_id).value_or (bound);

        const auto reply_payload = authenticate_res_t{
            ensured.actor_id, authenticated.display_name, ensured.actor.node_rid
        };
        const auto reply_message = zlink::message_t::from_json (reply_payload);
        co_await stream.reply_packet (reply_message).async ();

        co_return actor;
    }

  private:
    actor_ref_t to_actor_ref (const ensure_player_actor_res_t &ensured) const
    {
        return actor_ref_t (node_rid_t::from_string (ensured.actor.node_rid), ensured.actor_type,
                            ensured.actor.actor_id, ensured.actor.generation);
    }

    channel_client_t &_client;
    route_client_t &_routes;
    sample_topology_t &_topology;
};

} // namespace zlink::samples::bingo
