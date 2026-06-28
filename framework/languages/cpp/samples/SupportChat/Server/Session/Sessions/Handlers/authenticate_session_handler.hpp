/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Configuration/sample_names.hpp"
#include "../../../../Shared/Contracts/messages.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::supportchat
{

using namespace framework;
using framework::actor_ref_t;
using framework::message_t;

// Session lifecycle handler: validates the stream token through the API server,
// ensures a Support actor exists, and binds the current stream session to it.
class authenticate_session_handler_t
{
  public:
    using dependency_types = dependency_list_t<channel_client_t>;

    explicit authenticate_session_handler_t (channel_client_t &client) : _client (client) {}

    bool can_handle (const stream_dispatch_context_t &dispatch) const
    {
        return dispatch.packet_name () == authenticate_req_t::packet_name;
    }

    task_t<session_actor_t>
    handle (session_actor_manager_t &actors, stream_t &stream, const zlink::message_t &payload)
    {
        auto request = payload.parse_json<authenticate_req_t> ();
        const auto authenticate_request = authenticate_user_req_t{request.access_token};
        auto authenticated =
          co_await _client.request (sample_names_t::api_channel, authenticate_request)
            .async<authenticate_user_res_t> ();
        if (!authenticated.accepted || authenticated.actor_id.empty ()
            || authenticated.display_name.empty () || authenticated.role.empty ()) {
            co_return result_t<session_actor_t>::failure (framework_error_kind_t::request_failed,
                                                          authenticated.reason.empty ()
                                                            ? "SupportChat authentication failed."
                                                            : authenticated.reason);
        }

        auto create_request = ensure_support_user_actor_req_t{
          authenticated.actor_id, authenticated.display_name, authenticated.role};
        auto ensured = co_await _client.request (sample_names_t::support_channel, create_request)
                         .async<ensure_support_user_actor_res_t> ();
        auto bound = co_await actors.bind (to_actor_ref (ensured)).async ();
        if (ensured.actor.generation == 1
            && ensured.actor.node_rid == sample_names_t::support_spot_node) {
            auto joined =
              co_await bound.context ()
                .join_entry_spot (node_rid_t::from_string (ensured.actor.node_rid), create_request)
                .async ();
            if (joined.result_code != 0) {
                co_return result_t<session_actor_t>::failure (
                  framework_error_kind_t::request_failed,
                  "Support user entry spot join was rejected.");
            }
        }
        auto actor = actors.find (ensured.actor.actor_id).value_or (bound);

        const auto reply_payload = authenticate_res_t{
          ensured.actor.actor_id, authenticated.display_name, authenticated.role};
        const auto reply_message = zlink::message_t::from_json (reply_payload);
        co_await stream.reply_packet (reply_message).async ();

        co_return actor;
    }

  private:
    actor_ref_t to_actor_ref (const ensure_support_user_actor_res_t &ensured) const
    {
        return actor_ref_t (node_rid_t::from_string (ensured.actor.node_rid), ensured.actor_type,
                            ensured.actor.actor_id, ensured.actor.generation);
    }

    channel_client_t &_client;
};

} // namespace zlink::samples::supportchat
