/* SPDX-License-Identifier: MPL-2.0 */

#include "session_server_host_factory.hpp"

int
main ()
{
  using namespace zlink::samples::bingo;

  const sample_topology_t topology;
  auto zlink = session_server_host_factory_t::build (topology);
  if (zlink.streams ().size () != 1 ||
      !zlink.streams ()[0].actor_gateway_spot_node_name ||
      *zlink.streams ()[0].actor_gateway_spot_node_name !=
        sample_names_t::room_spot_node) {
    return 1;
  }

  authenticate_player_handler_t auth_handler;
  ensure_player_actor_handler_t actor_handler;
  const auto authenticated =
    auth_handler.handle ({ "player-1" });
  if (!authenticated.accepted) {
    return 2;
  }

  const auto ensured = actor_handler.handle (
    { authenticated.actor_id, authenticated.display_name });
  if (ensured.actor_type != sample_names_t::player_actor_type ||
      ensured.actor.actor_id != authenticated.actor_id) {
    return 3;
  }

  zlink::framework::session_actor_manager_t actors;
  auto created = actors.create (sample_names_t::player_actor_type,
                                authenticated.actor_id);
  if (!created) {
    return 4;
  }
  auto bound = actors.bind (zlink::framework::actor_ref_t (
                       zlink::framework::node_rid_t::from_string (
                         "bingo-play"),
                       sample_names_t::player_actor_type,
                       authenticated.actor_id,
                       1))
                 .submit ()
                 .result ();
  if (!bound) {
    return 5;
  }

  zlink::framework::stream_metadata_t metadata;
  metadata.with ("session", authenticated.actor_id)
    .with ("actor", authenticated.actor_id);
  zlink::framework::stream_header_t header (
    zlink::framework::stream_message_kind_t::request,
    zlink::framework::stream_codec_t::json,
    zlink::framework::stream_header_flags_t::has_metadata,
    1,
    authenticate_req_t::packet_name,
    metadata);
  const auto payload =
    zlink::message_t::from (std::string ("access_token=player-1"));
  if (!bound.value ().relay (header, payload).submit ().result ()) {
    return 6;
  }

  auto push =
    bound.value ()
      .bound_session ()
      .send (game_started_notify_t {})
      .submit ()
      .result ();
  if (!push || !bound.value ().notify_disconnected ().submit ().result ()) {
    return 7;
  }
  actors.unbind_session (authenticated.actor_id);
  return 0;
}
