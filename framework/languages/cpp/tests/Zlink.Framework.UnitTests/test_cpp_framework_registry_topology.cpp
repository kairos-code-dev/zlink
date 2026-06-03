/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>

#include "runtime/actors/actor_gateway_runtime.hpp"
#include "runtime/channels/channel_runtime_manager.hpp"
#include "runtime/registry/registry_runtime.hpp"

#include <chrono>
#include <optional>
#include <set>
#include <string>

namespace
{

struct stage_spot_t
{
};

bool
is_protocol_error (const zlink::framework::result_t<void> &result)
{
  return !result &&
         result.error_kind () ==
           zlink::framework::framework_error_kind_t::request_protocol_error;
}

} // namespace

int
main ()
{
  using namespace std::chrono_literals;
  using zlink::framework::framework_error_kind_t;

  zlink::framework::zlink_builder_t zlink;
  zlink.node ("registry-node")
    .registry ([](zlink::framework::registry_builder_t &registry) {
      registry.registry_id ("local-registry")
        .bind ("tcp://0.0.0.0:5550", "tcp://0.0.0.0:5551")
        .heartbeat_interval (100ms)
        .heartbeat_timeout (500ms)
        .broadcast_interval (250ms)
        .add_peer ("tcp://registry-peer:5550");
    })
    .discovery ([](zlink::framework::discovery_builder_t &discovery) {
      discovery.connect_registry ("tcp://registry:5551");
    })
    .route_channel ("game.route")
    .channel ("game.route", [](zlink::framework::channel_builder_t &channel) {
      channel.enable_client (
        [](zlink::framework::capability_builder_t &client) {
          client.connect ("tcp://route-peer:7001");
        });
    })
    .spot_node ("play-actors",
                [](zlink::framework::spot_node_builder_t &spot_node) {
      spot_node.bind ("tcp://0.0.0.0:7101")
        .use_registry_spot_remote_addresses ()
        .add_spot<stage_spot_t> ("stage");
    });

  const auto validation = zlink.validate_registry ();
  if (!validation) {
    return 1;
  }
  const auto registry_options = zlink.registry_options ();
  if (registry_options.registry_id != "local-registry" ||
      registry_options.pub_endpoint != "tcp://0.0.0.0:5550" ||
      registry_options.router_endpoint != "tcp://0.0.0.0:5551" ||
      registry_options.peer_pub_endpoints.size () != 1) {
    return 2;
  }
  if (zlink.discovery_options ().registry_endpoints.size () != 1 ||
      zlink.route_channels ().size () != 1) {
    return 3;
  }

  auto query = zlink.registry_query ();
  const auto status = query.status ();
  if (status.state != zlink::framework::registry_state_t::running ||
      status.registry_id != "local-registry" || status.peer_count != 1) {
    return 4;
  }
  if (query.service_summary ().size () < 2 || query.topology ().size () < 2) {
    return 5;
  }
  const auto peers = query.member_peers ("game.route");
  if (peers.size () != 1 || peers[0].endpoint != "tcp://route-peer:7001") {
    return 6;
  }
  const auto before_lookup_count =
    query.monitoring_snapshot ().spot_lookup_count;

  const auto remote_rid =
    zlink::framework::spot_rid_t::from_string ("remote-stage");
  auto registry_runtime =
    zlink::framework::detail::registry_runtime_t::from (query);
  registry_runtime.add_spot_route (
    zlink::framework::spot_route_t {
      zlink::framework::node_rid_t::from_string ("remote-node"),
      remote_rid,
      "stage" });
  const auto stale_rid =
    zlink::framework::spot_rid_t::from_string ("stale-stage");
  registry_runtime.add_spot_route (
    zlink::framework::spot_route_t {
      zlink::framework::node_rid_t::from_string ("stale-node"),
      stale_rid,
      "stage" });
  registry_runtime.cleanup_stale_spot_routes (
    std::set<std::string> { std::string (remote_rid.value ()) });
  auto route = query.resolve_spot_remote_address (remote_rid);
  if (!route || route.value ().spot_name != "stage" ||
      route.value ().node_rid.value () != "remote-node") {
    return 7;
  }
  if (query.monitoring_snapshot ().spot_lookup_count !=
      before_lookup_count + 1) {
    return 8;
  }
  auto missing =
    query.resolve_spot_remote_address (
      zlink::framework::spot_rid_t::from_string ("missing"));
  if (missing ||
      missing.error_kind () != framework_error_kind_t::spot_route_not_found) {
    return 9;
  }
  auto stale = query.resolve_spot_remote_address (stale_rid);
  if (stale ||
      stale.error_kind () != framework_error_kind_t::spot_route_not_found) {
    return 22;
  }

  zlink::framework::zlink_builder_t no_discovery;
  no_discovery.node ("no-discovery")
    .route_channel ("game.route")
    .spot_node ("actors", [](zlink::framework::spot_node_builder_t &spot) {
      spot.use_registry_spot_remote_addresses ();
    });
  if (!is_protocol_error (no_discovery.validate_registry ())) {
    return 10;
  }

  zlink::framework::zlink_builder_t no_route;
  no_route.node ("no-route")
    .discovery ([](zlink::framework::discovery_builder_t &discovery) {
      discovery.connect_registry ("tcp://registry:5551");
    })
    .spot_node ("actors", [](zlink::framework::spot_node_builder_t &spot) {
      spot.use_registry_spot_remote_addresses ();
    });
  if (!is_protocol_error (no_route.validate_registry ())) {
    return 11;
  }

  zlink::framework::zlink_builder_t ambiguous_route;
  ambiguous_route.node ("ambiguous")
    .discovery ([](zlink::framework::discovery_builder_t &discovery) {
      discovery.connect_registry ("tcp://registry:5551");
    })
    .route_channel ("route-a")
    .route_channel ("route-b")
    .spot_node ("actors", [](zlink::framework::spot_node_builder_t &spot) {
      spot.use_registry_spot_remote_addresses ();
    });
  if (!is_protocol_error (ambiguous_route.validate_registry ())) {
    return 12;
  }

  zlink::framework::zlink_builder_t unknown_route;
  unknown_route.node ("unknown")
    .discovery ([](zlink::framework::discovery_builder_t &discovery) {
      discovery.connect_registry ("tcp://registry:5551");
    })
    .route_channel ("route-a")
    .spot_node ("actors", [](zlink::framework::spot_node_builder_t &spot) {
      spot.use_registry_spot_remote_addresses ("route-missing");
    });
  if (!is_protocol_error (unknown_route.validate_registry ())) {
    return 13;
  }

  bool resolver_conflict_failed = false;
  try {
    zlink::framework::spot_node_builder_t spot;
    spot.use_registry_spot_remote_addresses ().add_spot_resolver (
      "custom",
      [](zlink::framework::spot_rid_t) {
        return std::optional<zlink::framework::spot_route_t> {};
      });
  } catch (const zlink::framework::framework_exception_t &error) {
    resolver_conflict_failed =
      error.kind () == framework_error_kind_t::request_protocol_error;
  }
  if (!resolver_conflict_failed) {
    return 14;
  }

  zlink::framework::detail::actor_gateway_runtime_t gateway;
  auto actor = gateway.manager ().bind (zlink::framework::actor_ref_t (
                             zlink::framework::node_rid_t::from_string (
                               "remote-node"),
                             "player",
                             "alice",
                             1))
                 .submit ()
                 .result ();
  if (!actor) {
    return 15;
  }
  const auto lookup_after_actor_bind =
    query.monitoring_snapshot ().spot_lookup_count;
  const auto payload = zlink::message_t::from (std::string ("payload"));
  zlink::framework::stream_header_t header (
    zlink::framework::stream_message_kind_t::send,
    zlink::framework::stream_codec_t::json,
    zlink::framework::stream_header_flags_t::none,
    std::nullopt,
    "move");
  auto relay = actor.value ().relay (header, payload).submit ().result ();
  if (!relay ||
      query.monitoring_snapshot ().spot_lookup_count !=
        lookup_after_actor_bind) {
    return 16;
  }

  zlink::framework::service_collection_t services;
  zlink::framework::handler_registry_t handlers;
  zlink::framework::serializer_registry_t serializers;
  zlink::framework::zlink_builder_t framework_zlink;
  zlink::framework::monitoring_builder_t monitoring;
  zlink::framework::zlink_framework_options_t options (
    services, handlers, serializers, framework_zlink, monitoring);
  options.discovery ().add ("tcp://registry:5551");
  options.use_registry_spot_remote_addresses ("game.route");
  options.route_mesh_channel ("game.route")
    .bind ("tcp://0.0.0.0:7200")
    .routing_id (zlink::routing_id_t::from ("7200"))
    .connect ("tcp://peer:7201");
  options.spot_mesh ("game.spots")
    .node ("game-node")
    .enable_router ("tcp://0.0.0.0:7300",
                    zlink::routing_id_t::from ("7300"))
    .enable_pub_sub ("tcp://0.0.0.0:7301",
                     zlink::routing_id_t::from ("7301"))
    .accept_routes_from_channel ("game.route")
    .add_spot<stage_spot_t> ("stage");
  options.apply ();
  if (framework_zlink.route_channels ().size () != 1 ||
      framework_zlink.route_channels ()[0] != "game.route") {
    return 17;
  }
  const auto framework_spots = framework_zlink.spot_nodes ();
  if (framework_spots.size () != 1 ||
      framework_spots[0].name != "game-node" ||
      framework_spots[0].bind_endpoint != "tcp://0.0.0.0:7300" ||
      !framework_spots[0].router_bind_endpoint ||
      *framework_spots[0].router_bind_endpoint != "tcp://0.0.0.0:7300" ||
      !framework_spots[0].pub_bind_endpoint ||
      *framework_spots[0].pub_bind_endpoint != "tcp://0.0.0.0:7301" ||
      !framework_spots[0].router_routing_id ||
      framework_spots[0].router_routing_id->to_string () != "7300" ||
      !framework_spots[0].pub_routing_id ||
      framework_spots[0].pub_routing_id->to_string () != "7301" ||
      !framework_spots[0].discovery_channel_name ||
      *framework_spots[0].discovery_channel_name != "game.spots" ||
      !framework_spots[0].registry_spot_remote_addresses_enabled ||
      !framework_spots[0].registry_spot_route_channel ||
      *framework_spots[0].registry_spot_route_channel != "game.route") {
    return 18;
  }
  if (!framework_zlink.validate_registry ()) {
    return 19;
  }
  auto route_manager =
    zlink::framework::detail::channel_runtime_manager_t::from (
      framework_zlink);
  route_manager.initialize_route_channels (framework_zlink);
  const auto &route_runtime = route_manager.get_route_channel ("game.route");
  if (!route_runtime.routing_id () ||
      route_runtime.routing_id ()->to_string () != "7200") {
    return 20;
  }

  zlink::framework::zlink_builder_t late_registry_zlink;
  zlink::framework::zlink_framework_options_t late_options (
    services, handlers, serializers, late_registry_zlink, monitoring);
  late_options.discovery ().add ("tcp://registry:5551");
  late_options.route_mesh_channel ("late.route")
    .bind ("tcp://0.0.0.0:7400")
    .routing_id (zlink::routing_id_t::from ("7400"));
  late_options.spot_mesh ("late.spots")
    .node ("late-node")
    .enable_router ("tcp://0.0.0.0:7500")
    .enable_pub_sub ("tcp://0.0.0.0:7501")
    .add_spot<stage_spot_t> ("stage");
  late_options.use_registry_spot_remote_addresses ("late.route");
  late_options.apply ();
  const auto late_spots = late_registry_zlink.spot_nodes ();
  if (late_spots.size () != 1 ||
      late_spots[0].spot_names.size () != 1 ||
      !late_spots[0].router_bind_endpoint ||
      !late_spots[0].pub_bind_endpoint ||
      !late_spots[0].registry_spot_remote_addresses_enabled ||
      !late_spots[0].registry_spot_route_channel ||
      *late_spots[0].registry_spot_route_channel != "late.route") {
    return 21;
  }

  return 0;
}
