/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Api/Handlers/authenticate_actor_handler.hpp"
#include "../../../Play/Handlers/ensure_player_actor_handler.hpp"

#include <zlink/codec/json.hpp>
#include <zlink/framework.hpp>

#include <string>
#include <utility>

namespace zlink::samples::tictactoe
{

class authenticate_session_packet_handler_t
{
public:
  authenticate_session_packet_handler_t (
    authenticate_actor_handler_t &authenticate,
    ensure_player_actor_handler_t &ensure_actor,
    std::string play_node_name)
    : _authenticate (authenticate),
      _ensure_actor (ensure_actor),
      _play_node_name (std::move (play_node_name))
  {
  }

  bool can_handle (const zlink::framework::stream_header_t &header) const
  {
    return header.packet_name () == authenticate_req_t::packet_name;
  }

  zlink::framework::task_t<zlink::framework::session_actor_t> handle (
    zlink::framework::session_actor_manager_t &actors,
    zlink::framework::stream_t &stream,
    const zlink::framework::stream_header_t &header,
    const zlink::message_t &payload)
  {
    const auto request = payload.parse_json<authenticate_req_t> ();
    const auto authenticated =
      _authenticate.handle ({ request.actor_id });
    if (!authenticated.accepted || authenticated.actor_id.empty ()) {
      co_return zlink::framework::result_t<zlink::framework::session_actor_t>::
        failure (zlink::framework::framework_error_kind_t::request_failed,
                 authenticated.reason.empty ()
                   ? "Actor authentication failed."
                   : authenticated.reason);
    }

    const auto ensured = _ensure_actor.handle ({ authenticated.actor_id });
    auto bound = co_await actors.bind (to_actor_ref (ensured)).submit ();

    co_await stream
      .reply_packet (
        header,
        zlink::message_t::from_json (
          authenticate_res_t { ensured.actor_id }))
      .submit ();

    co_return bound;
  }

private:
  zlink::framework::actor_ref_t to_actor_ref (
    const ensure_player_actor_res_t &ensured) const
  {
    return zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string (_play_node_name),
      ensured.actor_type,
      ensured.actor.actor_id,
      ensured.actor.generation);
  }

  authenticate_actor_handler_t &_authenticate;
  ensure_player_actor_handler_t &_ensure_actor;
  std::string _play_node_name;
};

} // namespace zlink::samples::tictactoe
