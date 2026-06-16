/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Handlers/ensure_player_actor_handler.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::tictactoe
{

class authenticate_play_session_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::channel_client_t,
                                          ensure_player_actor_handler_t>;

    authenticate_play_session_handler_t (zlink::framework::channel_client_t &client,
                                         ensure_player_actor_handler_t &ensure_actor) :
        _client (client), _ensure_actor (ensure_actor)
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
        if (!authenticated.accepted || authenticated.actor_id.empty ()) {
            co_return zlink::framework::result_t<zlink::framework::session_actor_t>::failure (
              zlink::framework::framework_error_kind_t::request_failed,
              authenticated.reason.empty () ? "Player authentication failed."
                                            : authenticated.reason);
        }

        const auto ensured = _ensure_actor.handle ({authenticated.actor_id});
        auto bound = co_await actors.bind (to_actor_ref (ensured)).async ();

        co_await stream
          .reply_packet (header, to_stream_payload (authenticate_res_t{ensured.actor_id}))
          .async ();

        co_return bound;
    }

  private:
    zlink::framework::actor_ref_t to_actor_ref (const ensure_player_actor_res_t &ensured) const
    {
        return zlink::framework::actor_ref_t (
          zlink::framework::node_rid_t::from_string (_play_node_name), ensured.actor_type,
          ensured.actor.actor_id, ensured.actor.generation);
    }

    zlink::framework::channel_client_t &_client;
    ensure_player_actor_handler_t &_ensure_actor;
    const char *_play_node_name = sample_names_t::spot_node;
};

} // namespace zlink::samples::tictactoe
