/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Configuration/sample_names.hpp"
#include "../../../../Shared/Contracts/messages.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::supportchat
{

// Session lifecycle handler: validates the stream token through the API server,
// ensures a Support actor exists, and binds the current stream session to it.
class authenticate_session_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<zlink::framework::channel_client_t>;

    explicit authenticate_session_handler_t (zlink::framework::channel_client_t &client) :
        _client (client)
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
        auto authenticated =
          co_await _client
            .request (sample_names_t::api_channel,
                      authenticate_user_req_t{request.access_token})
            .async<authenticate_user_res_t> ();
        if (!authenticated.accepted || authenticated.actor_id.empty ()
            || authenticated.display_name.empty () || authenticated.role.empty ()) {
            co_return zlink::framework::result_t<zlink::framework::session_actor_t>::failure (
              zlink::framework::framework_error_kind_t::request_failed,
              authenticated.reason.empty () ? "SupportChat authentication failed."
                                            : authenticated.reason);
        }

        auto create_request = ensure_support_user_actor_req_t{authenticated.actor_id,
                                                              authenticated.display_name,
                                                              authenticated.role};
        auto ensured =
          co_await _client
            .request (sample_names_t::support_channel, create_request)
            .async<ensure_support_user_actor_res_t> ();
        auto bound = co_await actors.bind (to_actor_ref (ensured)).async ();
        if (ensured.actor.generation == 1
            && ensured.actor.node_rid == sample_names_t::support_spot_node) {
            auto joined = co_await bound.context ()
                            .join_entry_spot (
                              zlink::framework::node_rid_t::from_string (ensured.actor.node_rid),
                              create_request)
                            .async ();
            if (joined.result_code != 0) {
                co_return zlink::framework::result_t<zlink::framework::session_actor_t>::failure (
                  zlink::framework::framework_error_kind_t::request_failed,
                  "Support user entry spot join was rejected.");
            }
        }
        auto actor = actors.find (ensured.actor.actor_id).value_or (bound);

        co_await stream
          .reply_packet (zlink::message_t::from_json (
            authenticate_res_t{ensured.actor.actor_id, authenticated.display_name,
                               authenticated.role}))
          .async ();

        co_return actor;
    }

  private:
    zlink::framework::actor_ref_t to_actor_ref (const ensure_support_user_actor_res_t &ensured) const
    {
        return zlink::framework::actor_ref_t (
          zlink::framework::node_rid_t::from_string (ensured.actor.node_rid),
          ensured.actor_type, ensured.actor.actor_id, ensured.actor.generation);
    }

    zlink::framework::channel_client_t &_client;
};

} // namespace zlink::samples::supportchat
