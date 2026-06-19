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
      zlink::framework::dependency_list_t<zlink::framework::channel_client_t, sample_topology_t>;

    explicit authenticate_session_handler_t (zlink::framework::channel_client_t &client,
                                             sample_topology_t &topology) :
        _client (client), _topology (topology)
    {
    }

    bool can_handle (const zlink::framework::stream_header_t &header) const
    {
        return header.packet_name () == authenticate_req_t::packet_name;
    }

    zlink::framework::task_t<zlink::framework::session_actor_t>
    handle (zlink::framework::session_actor_manager_t &actors,
            zlink::framework::stream_t &stream,
            const zlink::framework::stream_header_t &header,
            const zlink::message_t &payload)
    {
        authenticate_req_t request;
        from_stream_payload (payload, request);
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

        auto ensured = co_await _client
                         .request (
                           sample_names_t::play_channel,
                           ensure_player_actor_req_t{authenticated.actor_id,
                                                     authenticated.display_name,
                                                     _topology.preferred_play_node_rid ()})
                         .async<ensure_player_actor_res_t> ();
        auto bound = co_await actors.bind (to_actor_ref (ensured)).async ();

        co_await stream
          .reply_packet (header, to_stream_payload (authenticate_res_t{
                                   ensured.actor_id,
                                   authenticated.display_name,
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
    sample_topology_t &_topology;
};

} // namespace zlink::samples::bingo
