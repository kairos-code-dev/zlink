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
  int join_seen {};
  int packet_seen {};
  int joined_count {};
  int left_count {};
  int disconnected_count {};
  zlink::framework::spot_actor_change_kind_t last_change_kind =
    zlink::framework::spot_actor_change_kind_t::join_spot;
};

struct entry_spot_t
{
};

struct player_actor_factory_t
{
  int joined_value {};
  int moved_value {};
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

class state_update_handler_t
{
public:
  void handle (stage_spot_t &spot, const state_update_t &message)
  {
    spot.packet_seen = message.value;
    last_value = message.value;
  }

  int last_value {};
};

class move_join_handler_t
{
public:
  move_reply_t handle (stage_spot_t &spot,
                       player_actor_factory_t &actor,
                       const move_request_t &request)
  {
    spot.join_seen = request.value;
    actor.joined_value = request.value;
    return { request.value + 1 };
  }
};

class move_packet_handler_t
{
public:
  void handle (stage_spot_t &spot,
               player_actor_factory_t &actor,
               const zlink::framework::spot_actor_send_context_t &context,
               const move_request_t &request)
  {
    if (context.packet_name == "move") {
      spot.packet_seen = request.value;
    }
    actor.moved_value = request.value;
  }
};

class actor_joined_handler_t
{
public:
  void handle (stage_spot_t &spot,
               player_actor_factory_t &actor,
               const zlink::framework::spot_actor_change_result_t &result)
  {
    ++spot.joined_count;
    spot.last_change_kind = result.kind;
    actor.joined_value += 100;
  }
};

class actor_left_handler_t
{
public:
  void handle (stage_spot_t &spot,
               player_actor_factory_t &actor,
               const zlink::framework::spot_actor_change_result_t &result)
  {
    ++spot.left_count;
    spot.last_change_kind = result.kind;
    actor.moved_value += 100;
  }
};

class actor_disconnected_handler_t
{
public:
  void handle (stage_spot_t &spot, player_actor_factory_t &)
  {
    ++spot.disconnected_count;
  }
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

  context.handlers ()
    .add_handler<state_update_handler_t, stage_spot_t, state_update_t> (
      "state.update")
    .add_actor_packet<move_packet_handler_t,
                      stage_spot_t,
                      player_actor_factory_t,
                      move_request_t> ("move")
    .add_actor_join<move_join_handler_t,
                    stage_spot_t,
                    player_actor_factory_t,
                    move_request_t,
                    move_reply_t> ("join")
    .add_post_actor_joined<actor_joined_handler_t,
                           stage_spot_t,
                           player_actor_factory_t> ()
    .add_actor_left<actor_left_handler_t,
                    stage_spot_t,
                    player_actor_factory_t> ()
    .add_actor_disconnected<actor_disconnected_handler_t,
                            stage_spot_t,
                            player_actor_factory_t> ();
  const auto handler_descriptors = context.handlers ().descriptors ();
  if (handler_descriptors.size () != 6 ||
      handler_descriptors[0].kind !=
        zlink::framework::spot_handler_kind_t::packet ||
      handler_descriptors[0].packet_name != "state.update" ||
      handler_descriptors[1].kind !=
        zlink::framework::spot_handler_kind_t::actor_packet ||
      handler_descriptors[1].packet_name != "move" ||
      handler_descriptors[2].kind !=
        zlink::framework::spot_handler_kind_t::actor_join ||
      handler_descriptors[2].packet_name != "join" ||
      handler_descriptors[3].kind !=
        zlink::framework::spot_handler_kind_t::post_actor_joined ||
      handler_descriptors[4].kind !=
        zlink::framework::spot_handler_kind_t::actor_left ||
      handler_descriptors[5].kind !=
        zlink::framework::spot_handler_kind_t::actor_disconnected) {
    return 20;
  }

  zlink::framework::service_collection_t spot_services;
  spot_services.add_singleton<state_update_handler_t> ();
  spot_services.add_singleton<move_join_handler_t> ();
  spot_services.add_singleton<move_packet_handler_t> ();
  spot_services.add_singleton<actor_joined_handler_t> ();
  spot_services.add_singleton<actor_left_handler_t> ();
  spot_services.add_singleton<actor_disconnected_handler_t> ();
  auto spot_provider = spot_services.build_provider ();

  zlink::framework::serializer_registry_t spot_serializers;
  spot_serializers.add<state_update_t> (
    [](const state_update_t &value) {
      return zlink::message_t::from (std::to_string (value.value));
    },
    [](const zlink::message_t &message) {
      return state_update_t { std::stoi (message.to_string ()) };
    });
  spot_serializers.add<move_request_t> (
    [](const move_request_t &value) {
      return zlink::message_t::from (std::to_string (value.value));
    },
    [](const zlink::message_t &message) {
      return move_request_t { std::stoi (message.to_string ()) };
    });
  spot_serializers.add<move_reply_t> (
    [](const move_reply_t &value) {
      return zlink::message_t::from (std::to_string (value.value));
    },
    [](const zlink::message_t &message) {
      return move_reply_t { std::stoi (message.to_string ()) };
    });

  stage_spot_t stage_spot;
  player_actor_factory_t actor;
  const auto packet_dispatch = context.handlers ().invoke_packet (
    "state.update",
    stage_spot,
    spot_provider,
    spot_serializers,
    zlink::message_t::from (std::string ("30")));
  if (!packet_dispatch ||
      spot_provider.get_required<state_update_handler_t> ().last_value != 30 ||
      stage_spot.packet_seen != 30) {
    return 22;
  }

  const auto join_dispatch = context.handlers ().invoke_actor_join (
    "join",
    stage_spot,
    actor,
    spot_provider,
    spot_serializers,
    zlink::message_t::from (std::string ("41")));
  if (!join_dispatch ||
      spot_serializers.get<move_reply_t> ()
          .deserialize (join_dispatch.value ())
          .value != 42 ||
      actor.joined_value != 41 ||
      stage_spot.join_seen != 41) {
    return 23;
  }

  const auto move_dispatch = context.handlers ().invoke_actor_packet (
    "move",
    stage_spot,
    actor,
    spot_provider,
    spot_serializers,
    zlink::message_t::from (std::string ("55")));
  if (!move_dispatch || actor.moved_value != 55 ||
      stage_spot.packet_seen != 55) {
    return 24;
  }

  const auto joined_dispatch = context.handlers ().invoke_post_actor_joined (
    stage_spot,
    actor,
    spot_provider,
    spot_serializers,
    zlink::framework::spot_actor_change_result_t (
      zlink::framework::spot_actor_change_kind_t::join_spot));
  if (!joined_dispatch || stage_spot.joined_count != 1 ||
      actor.joined_value != 141 ||
      stage_spot.last_change_kind !=
        zlink::framework::spot_actor_change_kind_t::join_spot) {
    return 25;
  }

  const auto left_dispatch = context.handlers ().invoke_actor_left (
    stage_spot,
    actor,
    spot_provider,
    spot_serializers,
    zlink::framework::spot_actor_change_result_t (
      zlink::framework::spot_actor_change_kind_t::join_entry_spot));
  if (!left_dispatch || stage_spot.left_count != 1 ||
      actor.moved_value != 155 ||
      stage_spot.last_change_kind !=
        zlink::framework::spot_actor_change_kind_t::join_entry_spot) {
    return 26;
  }

  const auto disconnected_dispatch =
    context.handlers ().invoke_actor_disconnected (
      stage_spot, actor, spot_provider, spot_serializers);
  if (!disconnected_dispatch || stage_spot.disconnected_count != 1) {
    return 27;
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

  bool duplicate_handler_failed = false;
  try {
    context.handlers ()
      .add_handler<state_update_handler_t, stage_spot_t, state_update_t> (
        "state.update");
  } catch (const zlink::framework::framework_exception_t &error) {
    duplicate_handler_failed =
      error.kind () == framework_error_kind_t::request_protocol_error;
  }
  if (!duplicate_handler_failed) {
    return 21;
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
