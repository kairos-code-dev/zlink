/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>

#include <optional>
#include <string>

namespace
{

struct entry_spot_t
{
};

struct game_actor_factory_t
{
};

struct move_packet_t
{
};

} // namespace

int
main ()
{
  zlink::framework::zlink_builder_t zlink;
  zlink.node ("tictactoe-node")
    .spot_node ("game-actors",
                [](zlink::framework::spot_node_builder_t &spot_node) {
      spot_node.bind ("tcp://0.0.0.0:8101")
        .enable_actor_gateway ()
        .add_entry_spot<entry_spot_t> ()
        .add_actor_factory<game_actor_factory_t> ("game");
    })
    .stream ("client-stream", [](zlink::framework::stream_builder_t &stream) {
      stream.bind ("tcp://0.0.0.0:9200")
        .packet_session ("client")
        .attach_actor_gateway ("game-actors");
    });

  if (zlink.streams ().size () != 1 ||
      !zlink.streams ()[0].actor_gateway_spot_node_name ||
      *zlink.streams ()[0].actor_gateway_spot_node_name != "game-actors" ||
      zlink.spot_nodes ().size () != 1 ||
      !zlink.spot_nodes ()[0].actor_gateway_enabled ||
      zlink.spot_nodes ()[0].entry_spot_name != "entry" ||
      zlink.spot_nodes ()[0].actor_types.size () != 1) {
    return 1;
  }

  zlink::framework::session_actor_manager_t actors;
  auto created = actors.create ("game", "room-1");
  if (!created) {
    return 2;
  }

  auto actor = actors.bind (zlink::framework::actor_ref_t (
                         zlink::framework::node_rid_t::from_string (
                           "tictactoe-node"),
                         "game",
                         "room-1",
                         1))
                 .submit ()
                 .result ();
  if (!actor) {
    return 3;
  }

  zlink::framework::stream_metadata_t metadata;
  metadata.with ("session", "alice");
  zlink::framework::stream_header_t move_header (
    zlink::framework::stream_message_kind_t::send,
    zlink::framework::stream_codec_t::json,
    zlink::framework::stream_header_flags_t::has_metadata,
    std::nullopt,
    "move",
    metadata);
  const auto payload = zlink::message_t::from (std::string ("x:1,y:1"));
  if (!actor.value ().relay (move_header, payload).submit ().result ()) {
    return 4;
  }

  auto push =
    actor.value ().bound_session ().send (move_packet_t {}).submit ().result ();
  if (!push) {
    return 5;
  }

  auto joined = actors.get_or_create ("game", "room-1");
  if (!joined || joined.value ().actor_id () != "room-1") {
    return 6;
  }

  if (!actor.value ().notify_disconnected ().submit ().result ()) {
    return 7;
  }
  actors.unbind_session ("room-1");

  return 0;
}
