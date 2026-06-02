/* SPDX-License-Identifier: MPL-2.0 */

#include "session_server_host_factory.hpp"

int
main ()
{
  using namespace zlink::samples::tictactoe;

  const sample_topology_t topology;
  auto zlink = session_server_host_factory_t::build (topology);
  if (zlink.streams ().size () != 1 ||
      !zlink.streams ()[0].actor_gateway_spot_node_name ||
      *zlink.streams ()[0].actor_gateway_spot_node_name !=
        sample_names_t::spot_node) {
    return 1;
  }

  zlink::framework::session_actor_manager_t actors;
  auto actor = actors.create (sample_names_t::actor_type, "match-1");
  if (!actor) {
    return 2;
  }
  auto bound = actors.bind (zlink::framework::actor_ref_t (
                       zlink::framework::node_rid_t::from_string (
                         "tictactoe-play"),
                       sample_names_t::actor_type,
                       "match-1",
                       1))
                 .submit ()
                 .result ();
  if (!bound) {
    return 3;
  }

  zlink::framework::stream_metadata_t metadata;
  metadata.with ("session", "player-1").with ("match", "match-1");
  zlink::framework::stream_header_t move_header (
    zlink::framework::stream_message_kind_t::send,
    zlink::framework::stream_codec_t::json,
    zlink::framework::stream_header_flags_t::has_metadata,
    std::nullopt,
    place_mark_req_t::packet_name,
    metadata);
  const auto payload = zlink::message_t::from (std::string ("cell=0"));
  if (!bound.value ().relay (move_header, payload).submit ().result ()) {
    return 4;
  }

  auto push =
    bound.value ().bound_session ().send (move_packet_t { 2 }).submit ().result ();
  if (!push || !bound.value ().notify_disconnected ().submit ().result ()) {
    return 5;
  }
  actors.unbind_session ("match-1");
  return 0;
}
