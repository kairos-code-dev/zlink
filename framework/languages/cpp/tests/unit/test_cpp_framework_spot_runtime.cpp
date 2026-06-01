/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>

#include "runtime/spots/spot_runtime.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{

struct stage_spot_t
{
};

struct entry_spot_t
{
};

struct player_actor_factory_t
{
};

struct state_update_t
{
  int value {};
};

struct move_request_t
{
  int value {};
};

struct move_reply_t
{
  int value {};
};

struct stage_wrapper_t
{
  stage_wrapper_t (zlink::framework::node_rid_t node,
                   zlink::framework::spot_rid_t spot,
                   zlink::framework::publisher_t publisher,
                   std::size_t packets)
    : node_rid (std::move (node)),
      spot_rid (std::move (spot)),
      outbound (std::move (publisher)),
      packet_count (packets)
  {
  }

  void apply (int delta) { state += delta; }

  zlink::framework::node_rid_t node_rid;
  zlink::framework::spot_rid_t spot_rid;
  zlink::framework::publisher_t outbound;
  std::size_t packet_count {};
  int state {};
};

} // namespace

int
main ()
{
  using zlink::framework::framework_error_kind_t;

  zlink::framework::zlink_builder_t zlink;
  zlink.node ("stage-node")
    .channel ("game.stage", [](zlink::framework::channel_builder_t &channel) {
      channel.enable_publisher (
        [](zlink::framework::capability_builder_t &publisher) {
          publisher.bind ("tcp://127.0.0.1:8101");
        });
      channel.enable_subscriber (
        [](zlink::framework::capability_builder_t &subscriber) {
          subscriber.use_discovery ();
        });
    })
    .spot_node ("stage-spot-node",
                [](zlink::framework::spot_node_builder_t &spot_node) {
      spot_node.bind ("tcp://0.0.0.0:9000")
        .enable_actor_gateway ()
        .use_discovery ("game.stage")
        .attach_channel_client ("profile")
        .attach_publisher ("game.stage")
        .add_entry_spot<entry_spot_t> ()
        .add_actor_factory<player_actor_factory_t> ("player")
        .add_spot<stage_spot_t> ("stage");
    });

  const auto snapshots = zlink.spot_nodes ();
  if (snapshots.size () != 1 || snapshots[0].name != "stage-spot-node" ||
      snapshots[0].bind_endpoint != "tcp://0.0.0.0:9000" ||
      !snapshots[0].actor_gateway_enabled ||
      !snapshots[0].discovery_channel_name ||
      *snapshots[0].discovery_channel_name != "game.stage" ||
      snapshots[0].attached_channel_clients.size () != 1 ||
      snapshots[0].attached_publishers.size () != 1 ||
      snapshots[0].spot_names.size () != 2 ||
      snapshots[0].entry_spot_name != "entry" ||
      snapshots[0].actor_types.size () != 1) {
    return 1;
  }

  zlink::framework::spot_node_builder_t builder;
  zlink::framework::zlink_builder_t manual_host;
  manual_host.spot_node (
    "manual-stage",
    [&builder](zlink::framework::spot_node_builder_t &spot_node) {
      spot_node.bind ("tcp://0.0.0.0:9001")
        .use_discovery ("game.stage")
        .attach_publisher ("game.stage")
        .add_entry_spot<entry_spot_t> ()
        .add_actor_factory<player_actor_factory_t> ("player")
        .add_spot<stage_spot_t> ("stage");
      builder = spot_node;
    });

  auto context = builder.create_spot ("stage");
  if (context.node_rid ().empty () || context.spot_rid ().empty () ||
      context.spot_name () != "stage") {
    return 2;
  }

  const auto local_name = builder.spot_name_for (context.spot_rid ());
  if (!local_name || *local_name != "stage") {
    return 3;
  }

  const auto local_route = builder.resolve_spot (context.spot_rid ());
  if (!local_route || local_route->spot_name != "stage" ||
      local_route->node_rid.empty ()) {
    return 4;
  }

  const auto remote_rid =
    zlink::framework::spot_rid_t::from_string ("remote-stage");
  builder.add_spot_resolver (
    "remote",
    [remote_rid](zlink::framework::spot_rid_t rid)
      -> std::optional<zlink::framework::spot_route_t> {
      if (std::string (rid.value ()) != std::string (remote_rid.value ())) {
        return std::nullopt;
      }
      return zlink::framework::spot_route_t {
        zlink::framework::node_rid_t::from_string ("remote-node"),
        remote_rid,
        "remote-stage" };
    });
  const auto remote_route = builder.resolve_spot (remote_rid);
  if (!remote_route || remote_route->spot_name != "remote-stage") {
    return 5;
  }

  bool duplicate_spot_failed = false;
  try {
    builder.add_spot<stage_spot_t> ("stage");
  } catch (const zlink::framework::framework_exception_t &error) {
    duplicate_spot_failed =
      error.kind () == framework_error_kind_t::request_protocol_error;
  }
  if (!duplicate_spot_failed) {
    return 6;
  }

  bool duplicate_resolver_failed = false;
  try {
    builder.add_spot_resolver (
      "remote",
      [](zlink::framework::spot_rid_t) {
        return std::optional<zlink::framework::spot_route_t> {};
      });
  } catch (const zlink::framework::framework_exception_t &error) {
    duplicate_resolver_failed =
      error.kind () == framework_error_kind_t::request_protocol_error;
  }
  if (!duplicate_resolver_failed) {
    return 7;
  }

  bool empty_discovery_failed = false;
  try {
    zlink::framework::spot_node_builder_t invalid;
    invalid.use_discovery ("");
  } catch (const zlink::framework::framework_exception_t &error) {
    empty_discovery_failed =
      error.kind () == framework_error_kind_t::request_protocol_error;
  }
  if (!empty_discovery_failed) {
    return 8;
  }

  zlink::framework::spot_node_builder_t registry;
  registry.use_registry_spot_remote_addresses ("game.route");
  if (!registry.snapshot ().registry_spot_route_channel ||
      *registry.snapshot ().registry_spot_route_channel != "game.route") {
    return 9;
  }

  bool registry_conflict_failed = false;
  try {
    registry.add_spot_resolver (
      "custom",
      [](zlink::framework::spot_rid_t) {
        return std::optional<zlink::framework::spot_route_t> {};
      });
  } catch (const zlink::framework::framework_exception_t &error) {
    registry_conflict_failed =
      error.kind () == framework_error_kind_t::request_protocol_error;
  }
  if (!registry_conflict_failed) {
    return 10;
  }

  bool empty_registry_route_failed = false;
  try {
    zlink::framework::spot_node_builder_t invalid;
    invalid.use_registry_spot_remote_addresses ("");
  } catch (const zlink::framework::framework_exception_t &error) {
    empty_registry_route_failed =
      error.kind () == framework_error_kind_t::request_protocol_error;
  }
  if (!empty_registry_route_failed) {
    return 11;
  }

  context.register_packet<state_update_t> ("state.update");
  if (context.packet_registry ().size () != 1 ||
      context.packet_registry ()[0].packet_name != "state.update") {
    return 12;
  }

  bool duplicate_packet_failed = false;
  try {
    context.register_packet<state_update_t> ("state.update");
  } catch (const zlink::framework::framework_exception_t &error) {
    duplicate_packet_failed =
      error.kind () == framework_error_kind_t::request_protocol_error;
  }
  if (!duplicate_packet_failed) {
    return 13;
  }

  auto publish_result =
    context.publish ("stage.state.updated", state_update_t { 1 })
      .submit ()
      .result ();
  if (!publish_result) {
    return 14;
  }

  auto send_result =
    context.send_to (remote_route->node_rid, remote_route->spot_rid,
                     state_update_t { 2 })
      .submit ()
      .result ();
  if (!send_result) {
    return 15;
  }

  auto request_result =
    context
      .request_to<move_reply_t> (remote_route->node_rid, remote_route->spot_rid,
                                 move_request_t { 3 })
      .submit ()
      .result ();
  if (request_result ||
      request_result.error_kind () != framework_error_kind_t::timeout) {
    return 16;
  }

  auto missing_route_result =
    context
      .request_to<move_reply_t> (zlink::framework::node_rid_t {},
                                 zlink::framework::spot_rid_t {},
                                 move_request_t { 4 })
      .submit ()
      .result ();
  if (missing_route_result ||
      missing_route_result.error_kind () !=
        framework_error_kind_t::spot_route_not_found) {
    return 17;
  }

  const auto runtime =
    zlink::framework::detail::spot_node_runtime_t::from (builder);
  const auto &ordering = runtime.ordering_log (context);
  if (ordering.size () != 3 ||
      ordering[0] != "publish:stage.state.updated" ||
      ordering[1] != "send_to:remote-stage" ||
      ordering[2] != "request_to:remote-stage") {
    return 18;
  }

  stage_wrapper_t wrapper (
    context.node_rid (),
    context.spot_rid (),
    zlink.publisher (),
    context.packet_registry ().size ());
  wrapper.apply (7);
  if (wrapper.node_rid.empty () || wrapper.spot_rid.empty () ||
      wrapper.packet_count != 1 || wrapper.state != 7) {
    return 19;
  }

  return 0;
}
