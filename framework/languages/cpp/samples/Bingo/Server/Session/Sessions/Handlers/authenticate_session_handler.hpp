/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../../Configuration/sample_names.hpp"
#include "../../../Configuration/sample_topology.hpp"
#include "../../../../Shared/Contracts/messages.hpp"

#include "../../../../Shared/Contracts/protobuf_conversions.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::bingo
{

using namespace framework;
using framework::actor_ref_t;
using framework::message_t;

class authenticate_session_handler_t
{
  public:
    using dependency_types =
      dependency_list_t<channel_client_t, route_client_t, sample_topology_t,
                        spot_handle_resolver_t>;

    explicit authenticate_session_handler_t (channel_client_t &client,
                                             route_client_t &routes,
                                             sample_topology_t &topology,
                                             spot_handle_resolver_t &spot_handles) :
        _client (client), _routes (routes), _topology (topology), _spot_handles (spot_handles)
    {
    }

    bool can_handle (const stream_dispatch_context_t &dispatch) const
    {
        return dispatch.packet_name () == authenticate_req_t::packet_name;
    }

    task_t<session_actor_t>
    handle (session_actor_manager_t &actors, stream_t &stream, const zlink::message_t &payload)
    {
        /* client stream의 payload도 Protobuf다 — JSON으로 파싱하지 않는다. */
        authenticate_req_t request;
        from_stream_payload (payload, request);
        const auto authenticate_request = authenticate_player_req_t{request.access_token};
        auto authenticated = co_await _client.request (
            sample_names_t::api_channel, authenticate_request).submit<authenticate_player_res_t> ();
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
        auto play_entry_spot = co_await _spot_handles.resolve_spot_handle (
          spot_rid_t::from_string (_topology.preferred_play_node_rid ()));
        if (!play_entry_spot) {
            co_return result_t<session_actor_t>::failure (
              framework_error_kind_t::spot_route_not_found,
              "Play entry spot '" + _topology.preferred_play_node_rid ()
                + "' has no live location row.");
        }
        auto ensured = co_await _routes
            .request_to_spot (*play_entry_spot, create_request)
            .submit<ensure_player_actor_res_t> ();
        auto bound =
          co_await actors.bind_or_get (ensured.actor.to_actor_ref (ensured.actor_type)).submit ();
        auto actor = actors.find (ensured.actor.actor_id).value_or (bound);

        const auto reply_payload = authenticate_res_t{
            ensured.actor_id, authenticated.display_name,
            std::string (ensured.actor.node_rid.value ())
        };
        const auto reply_message = to_stream_payload (reply_payload);
        stream.reply_packet (reply_message).submit ();

        co_return actor;
    }

  private:
    channel_client_t &_client;
    route_client_t &_routes;
    sample_topology_t &_topology;
    spot_handle_resolver_t &_spot_handles;
};

} // namespace zlink::samples::bingo
