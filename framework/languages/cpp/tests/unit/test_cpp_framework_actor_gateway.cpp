/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>

#include "runtime/actors/actor_gateway_runtime.hpp"

#include <string>

int
main ()
{
  using zlink::framework::framework_error_kind_t;

  zlink::framework::zlink_builder_t zlink;
  zlink.spot_node ("session-actors",
                   [](zlink::framework::spot_node_builder_t &spot_node) {
    spot_node.bind ("tcp://0.0.0.0:7101").enable_actor_gateway ();
  }).stream ("client-stream",
             [](zlink::framework::stream_builder_t &stream) {
    stream.bind ("tcp://0.0.0.0:9200")
      .packet_session ("client")
      .attach_actor_gateway ("session-actors");
  });

  if (zlink.streams ().size () != 1 ||
      !zlink.streams ()[0].actor_gateway_spot_node_name ||
      *zlink.streams ()[0].actor_gateway_spot_node_name != "session-actors") {
    return 1;
  }

  zlink::framework::detail::actor_gateway_runtime_t gateway;
  auto manager = gateway.manager ();
  auto created = manager.create ("player", "alice");
  if (!created || created.value ().actor_id () != "alice") {
    return 2;
  }

  auto duplicate = manager.create ("player", "alice");
  if (duplicate ||
      duplicate.error_kind () != framework_error_kind_t::actor_already_exists) {
    return 3;
  }

  auto found = manager.find ("alice");
  if (!found || found->ref ().actor_type () != "player") {
    return 4;
  }

  auto type_mismatch = manager.get_or_create ("enemy", "alice");
  if (type_mismatch ||
      type_mismatch.error_kind () != framework_error_kind_t::actor_type_mismatch) {
    return 5;
  }

  zlink::framework::actor_ref_t remote_ref (
    zlink::framework::node_rid_t::from_string ("remote-node"),
    "player",
    "bob",
    7);
  auto bound = manager.bind (remote_ref).submit ().result ();
  if (!bound || !gateway.actor_bound ("bob") ||
      bound.value ().ref ().generation () != 7) {
    return 6;
  }

  zlink::framework::stream_metadata_t metadata;
  metadata.with ("trace", "t1");
  zlink::framework::stream_header_t header (
    zlink::framework::stream_message_kind_t::send,
    zlink::framework::stream_codec_t::json,
    zlink::framework::stream_header_flags_t::has_metadata,
    std::nullopt,
    "move",
    metadata);
  const auto payload = zlink::message_t::from (std::string ("payload"));
  auto relay = bound.value ().relay (header, payload).submit ().result ();
  if (!relay || gateway.relayed_frames ().size () != 1 ||
      gateway.relayed_frames ()[0].payload.to_string () != "payload" ||
      payload.to_string () != "payload") {
    return 7;
  }

  zlink::framework::session_actor_t unbound;
  auto missing_relay = unbound.relay (header, payload).submit ().result ();
  if (missing_relay ||
      missing_relay.error_kind () != framework_error_kind_t::actor_route_not_found) {
    return 8;
  }

  auto push_result =
    bound.value ().context ().bound_session ().send_raw (payload).submit ().result ();
  if (!push_result || gateway.bound_session_pushes ().size () != 1 ||
      gateway.bound_session_pushes ()[0].payload.to_string () != "payload") {
    return 9;
  }

  auto disconnect = bound.value ().notify_disconnected ().submit ().result ();
  if (!disconnect || gateway.actor_bound ("bob") ||
      !gateway.actor_disconnected ("bob")) {
    return 10;
  }

  auto rebound = manager.bind (remote_ref).submit ().result ();
  if (!rebound || !gateway.actor_bound ("bob")) {
    return 11;
  }
  manager.unbind_session ("bob");
  if (gateway.actor_bound ("bob") || !gateway.actor_disconnected ("bob")) {
    return 12;
  }

  return 0;
}
