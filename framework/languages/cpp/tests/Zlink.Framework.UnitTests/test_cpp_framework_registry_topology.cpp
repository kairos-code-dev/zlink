/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>
#include <zlink.hpp>

#include "runtime/actors/actor_gateway_runtime.hpp"
#include "runtime/channels/channel_runtime_manager.hpp"
#include "runtime/registry/registry_runtime.hpp"

#include <chrono>
#include <functional>
#include <optional>
#include <sstream>
#include <set>
#include <string>
#include <thread>

#if defined(ZLINK_HAVE_WINDOWS)
#include <process.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace
{

struct stage_spot_t : public zlink::framework::spot_t
{
};

bool is_protocol_error (const zlink::framework::result_t<void> &result)
{
    return !result
           && result.error_kind ()
                == zlink::framework::framework_error_kind_t::request_protocol_error;
}

std::string unique_tcp (const char *base)
{
#if !defined(ZLINK_HAVE_WINDOWS)
    const int fd = socket (AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
        address.sin_port = 0;
        if (bind (fd, reinterpret_cast<sockaddr *> (&address), sizeof (address)) == 0) {
            socklen_t length = sizeof (address);
            if (getsockname (fd, reinterpret_cast<sockaddr *> (&address), &length) == 0) {
                const auto port = ntohs (address.sin_port);
                close (fd);
                std::ostringstream stream;
                stream << "tcp://127.0.0.1:" << port;
                return stream.str ();
            }
        }
        close (fd);
    }
#endif
    static unsigned counter = 0;
    const unsigned pid =
#if defined(ZLINK_HAVE_WINDOWS)
      static_cast<unsigned> (_getpid ());
#else
      static_cast<unsigned> (getpid ());
#endif
    const auto now =
      static_cast<unsigned> (std::chrono::steady_clock::now ().time_since_epoch ().count ());
    const auto salt = static_cast<unsigned> (std::hash<std::string>{}(base));
    const unsigned port = 20000u + ((pid * 131u + now + salt + (++counter * 17u)) % 30000u);
    std::ostringstream stream;
    stream << "tcp://127.0.0.1:" << port;
    return stream.str ();
}

} // namespace

int main ()
{
    using namespace std::chrono_literals;
    using zlink::framework::framework_error_kind_t;

    const auto embedded_registry_pub = unique_tcp ("embedded-registry-pub");
    const auto embedded_registry_router = unique_tcp ("embedded-registry-router");
    const auto embedded_spot_endpoint = unique_tcp ("embedded-spot");
    zlink::framework::zlink_builder_t zlink;
    zlink.add_node ("registry-node")
      .enable_registry ([&] (zlink::framework::registry_builder_t &registry) {
          registry.registry_id ("local-registry")
            .bind (embedded_registry_pub, embedded_registry_router)
            .heartbeat_interval (100ms)
            .heartbeat_timeout (500ms)
            .broadcast_interval (250ms)
            .add_peer ("tcp://registry-peer:5550");
      })
      .discovery ([] (zlink::framework::discovery_builder_t &discovery) {
          discovery.connect_registry ("tcp://registry:5551");
      })
      .route_channel ("game.route")
      .channel ("game.route",
                [] (zlink::framework::channel_builder_t &channel) {
                    channel.enable_client ([] (zlink::framework::capability_builder_t &client) {
                        client.connect ("tcp://route-peer:7001");
                    });
                })
      .add_spot_node ("play-actors", [&] (zlink::framework::spot_node_builder_t &spot_node) {
          spot_node.bind (embedded_spot_endpoint)
            .use_registry_spot_remote_addresses ()
            .add_spot<stage_spot_t> ("stage");
      });

    const auto validation = zlink.validate_registry ();
    if (!validation) {
        return 1;
    }
    const auto registry_options = zlink.registry_options ();
    if (registry_options.registry_id != "local-registry"
        || registry_options.pub_endpoint != embedded_registry_pub
        || registry_options.router_endpoint != embedded_registry_router
        || registry_options.peer_pub_endpoints.size () != 1) {
        return 2;
    }
    if (zlink.discovery_options ().registry_endpoints.size () != 1
        || zlink.route_channels ().size () != 1) {
        return 3;
    }

    auto query = zlink.registry_query ();
    const auto status = query.status ();
    if (status.state != zlink::framework::registry_state_t::running
        || status.registry_id != "local-registry" || status.peer_count != 1) {
        return 4;
    }
    if (query.service_summary ().size () < 2 || query.topology ().size () < 2) {
        return 5;
    }
    zlink::framework::service_summary_filter_t route_service_filter;
    route_service_filter.name = "game.route";
    route_service_filter.kind = zlink::framework::service_kind_t::channel;
    const auto route_services = query.service_summary (route_service_filter);
    if (route_services.size () != 1
        || route_services[0].role != zlink::framework::service_role_t::client) {
        return 23;
    }
    zlink::framework::topology_filter_t spot_topology_filter;
    spot_topology_filter.node_name = "registry-node";
    spot_topology_filter.name = "play-actors";
    spot_topology_filter.kind = zlink::framework::service_kind_t::spot;
    spot_topology_filter.role = zlink::framework::service_role_t::spot_node;
    spot_topology_filter.source = zlink::framework::topology_source_t::embedded;
    spot_topology_filter.state = zlink::framework::topology_state_t::active;
    const auto spot_topology = query.topology (spot_topology_filter);
    if (spot_topology.size () != 1 || spot_topology[0].name != "play-actors") {
        return 24;
    }
    spot_topology_filter.name = "missing";
    if (!query.topology (spot_topology_filter).empty ()) {
        return 25;
    }
    zlink::framework::registry_query_client_t disconnected_client;
    const auto disconnected_topology = disconnected_client.topology ();
    if (disconnected_topology
        || disconnected_topology.error_kind ()
             != zlink::framework::framework_error_kind_t::disconnected) {
        return 26;
    }
    zlink::framework::registry_query_client_options_t missing_endpoint;
    const auto missing_endpoint_result = disconnected_client.connect (missing_endpoint);
    if (missing_endpoint_result
        || missing_endpoint_result.error_kind ()
             != zlink::framework::framework_error_kind_t::request_protocol_error) {
        return 27;
    }
    const auto peers = query.member_peers ("game.route");
    if (peers.size () != 1 || peers[0].endpoint != "tcp://route-peer:7001") {
        return 6;
    }
    const auto before_lookup_count = query.monitoring_snapshot ().spot_lookup_count;

    const auto remote_rid = zlink::framework::spot_rid_t::from_string ("remote-stage");
    auto registry_runtime = zlink::framework::detail::registry_runtime_t::from (query);
    registry_runtime.add_spot_route (zlink::framework::spot_route_t{
      zlink::framework::node_rid_t::from_string ("remote-node"), remote_rid, "stage"});
    const auto stale_rid = zlink::framework::spot_rid_t::from_string ("stale-stage");
    registry_runtime.add_spot_route (zlink::framework::spot_route_t{
      zlink::framework::node_rid_t::from_string ("stale-node"), stale_rid, "stage"});
    registry_runtime.cleanup_stale_spot_routes (
      std::set<std::string>{std::string (remote_rid.value ())});
    auto route = query.resolve_spot_remote_address (remote_rid);
    if (!route || route.value ().spot_name != "stage"
        || route.value ().node_rid.value () != "remote-node") {
        return 7;
    }
    if (query.monitoring_snapshot ().spot_lookup_count != before_lookup_count + 1) {
        return 8;
    }
    auto missing =
      query.resolve_spot_remote_address (zlink::framework::spot_rid_t::from_string ("missing"));
    if (missing || missing.error_kind () != framework_error_kind_t::spot_route_not_found) {
        return 9;
    }
    auto stale = query.resolve_spot_remote_address (stale_rid);
    if (stale || stale.error_kind () != framework_error_kind_t::spot_route_not_found) {
        return 22;
    }

    zlink::framework::zlink_builder_t no_discovery;
    no_discovery.add_node ("no-discovery")
      .route_channel ("game.route")
      .add_spot_node ("actors", [] (zlink::framework::spot_node_builder_t &spot) {
          spot.use_registry_spot_remote_addresses ();
      });
    if (!is_protocol_error (no_discovery.validate_registry ())) {
        return 10;
    }

    zlink::framework::zlink_builder_t no_route;
    no_route.add_node ("no-route")
      .discovery ([] (zlink::framework::discovery_builder_t &discovery) {
          discovery.connect_registry ("tcp://registry:5551");
      })
      .add_spot_node ("actors", [] (zlink::framework::spot_node_builder_t &spot) {
          spot.use_registry_spot_remote_addresses ();
      });
    if (!is_protocol_error (no_route.validate_registry ())) {
        return 11;
    }

    zlink::framework::zlink_builder_t ambiguous_route;
    ambiguous_route.add_node ("ambiguous")
      .discovery ([] (zlink::framework::discovery_builder_t &discovery) {
          discovery.connect_registry ("tcp://registry:5551");
      })
      .route_channel ("route-a")
      .route_channel ("route-b")
      .add_spot_node ("actors", [] (zlink::framework::spot_node_builder_t &spot) {
          spot.use_registry_spot_remote_addresses ();
      });
    if (!is_protocol_error (ambiguous_route.validate_registry ())) {
        return 12;
    }

    zlink::framework::zlink_builder_t unknown_route;
    unknown_route.add_node ("unknown")
      .discovery ([] (zlink::framework::discovery_builder_t &discovery) {
          discovery.connect_registry ("tcp://registry:5551");
      })
      .route_channel ("route-a")
      .add_spot_node ("actors", [] (zlink::framework::spot_node_builder_t &spot) {
          spot.use_registry_spot_remote_addresses ("route-missing");
      });
    if (!is_protocol_error (unknown_route.validate_registry ())) {
        return 13;
    }

    bool resolver_conflict_failed = false;
    try {
        zlink::framework::spot_node_builder_t spot;
        spot.use_registry_spot_remote_addresses ().add_spot_resolver (
          "custom", [] (zlink::framework::spot_rid_t) {
              return std::optional<zlink::framework::spot_route_t>{};
          });
    }
    catch (const zlink::framework::framework_exception_t &error) {
        resolver_conflict_failed = error.kind () == framework_error_kind_t::request_protocol_error;
    }
    if (!resolver_conflict_failed) {
        return 14;
    }

    zlink::framework::detail::actor_gateway_runtime_t gateway;
    auto actor =
      gateway.manager ()
        .bind (zlink::framework::actor_ref_t (
          zlink::framework::node_rid_t::from_string ("remote-node"), "player", "alice", 1))
        .submit ()
        .result ();
    if (!actor) {
        return 15;
    }
    const auto lookup_after_actor_bind = query.monitoring_snapshot ().spot_lookup_count;
    const auto payload = zlink::message_t::from (std::string ("payload"));
    zlink::framework::stream_header_t header (
      zlink::framework::stream_message_kind_t::send, zlink::framework::stream_codec_t::json,
      zlink::framework::stream_header_flags_t::none, std::nullopt, "move");
    auto relay = actor.value ().relay (header, payload).submit ().result ();
    if (!relay || query.monitoring_snapshot ().spot_lookup_count != lookup_after_actor_bind) {
        return 16;
    }

    zlink::framework::service_collection_t services;
    zlink::framework::handler_registry_t handlers;
    zlink::framework::serializer_registry_t serializers;
    zlink::framework::zlink_builder_t framework_zlink;
    zlink::framework::monitoring_builder_t monitoring;
    zlink::framework::zlink_framework_options_t options (services, handlers, serializers,
                                                         framework_zlink, monitoring);
    const auto framework_route_endpoint = unique_tcp ("framework-route");
    const auto framework_router_endpoint = unique_tcp ("framework-router");
    const auto framework_pub_endpoint = unique_tcp ("framework-pub");
    options.use_discovery ().add_registry_endpoint ("tcp://registry:5551");
    options.use_registry_spot_remote_addresses ("game.route");
    options.add_route_mesh_channel ("game.route")
      .bind (framework_route_endpoint)
      .set_routing_id (zlink::routing_id_t::from ("7200"))
      .connect ("tcp://peer:7201")
      .enable_spot_route_egress ("game.route");
    options.add_spot_mesh ("game.spots")
      .add_node ("game-node")
      .enable_router (framework_router_endpoint,
                      [] (zlink::framework::spot_router_capability_builder_t &router) {
                          router.set_routing_id (zlink::routing_id_t::from ("7300"))
                            .connect ("tcp://router-peer:7302");
                      })
      .enable_pub_sub (framework_pub_endpoint,
                       [] (zlink::framework::spot_pub_sub_capability_builder_t &pub_sub) {
                           pub_sub.set_routing_id (zlink::routing_id_t::from ("7301"))
                             .connect ("tcp://pub-peer:7303");
                       })
      .accept_routes_from_channel ("game.route")
      .add_spot<stage_spot_t> ("stage");
    options.apply ();
    if (framework_zlink.route_channels ().size () != 1
        || framework_zlink.route_channels ()[0] != "game.route") {
        return 17;
    }
    const auto framework_spots = framework_zlink.spot_nodes ();
    if (framework_spots.size () != 1 || framework_spots[0].name != "game-node"
        || framework_spots[0].bind_endpoint != framework_router_endpoint
        || !framework_spots[0].router_bind_endpoint
        || *framework_spots[0].router_bind_endpoint != framework_router_endpoint
        || !framework_spots[0].pub_bind_endpoint
        || *framework_spots[0].pub_bind_endpoint != framework_pub_endpoint
        || !framework_spots[0].router_routing_id
        || framework_spots[0].router_routing_id->to_string () != "7300"
        || framework_spots[0].router_manual_connections.size () != 1
        || framework_spots[0].router_manual_connections[0] != "tcp://router-peer:7302"
        || !framework_spots[0].pub_routing_id
        || framework_spots[0].pub_routing_id->to_string () != "7301"
        || framework_spots[0].pub_sub_manual_connections.size () != 1
        || framework_spots[0].pub_sub_manual_connections[0] != "tcp://pub-peer:7303"
        || !framework_spots[0].discovery_channel_name
        || *framework_spots[0].discovery_channel_name != "game.spots"
        || !framework_spots[0].registry_spot_remote_addresses_enabled
        || !framework_spots[0].registry_spot_route_channel
        || *framework_spots[0].registry_spot_route_channel != "game.route"
        || framework_spots[0].accepted_route_channels.size () != 1
        || framework_spots[0].accepted_route_channels[0].channel_name != "game.route"
        || !framework_spots[0].accepted_route_channels[0].manual_connections.empty ()) {
        return 18;
    }
    if (!framework_zlink.validate_registry ()) {
        return 19;
    }
    auto route_manager =
      zlink::framework::detail::channel_runtime_manager_t::from (framework_zlink);
    route_manager.initialize_route_channels (framework_zlink);
    const auto &route_runtime = route_manager.get_route_channel ("game.route");
    if (!route_runtime.routing_id () || route_runtime.routing_id ()->to_string () != "7200"
        || !route_runtime.spot_route_egress_target ()
        || *route_runtime.spot_route_egress_target () != "game.route") {
        return 20;
    }

    zlink::framework::zlink_builder_t manual_route_zlink;
    zlink::framework::zlink_framework_options_t manual_route_options (
      services, handlers, serializers, manual_route_zlink, monitoring);
    const auto manual_route_endpoint = unique_tcp ("manual-accepted-route");
    const auto manual_route_router_endpoint = unique_tcp ("manual-accepted-router");
    manual_route_options.add_client_server_channel ("manual.api")
      .enable_server (manual_route_endpoint);
    manual_route_options.add_spot_node ("manual-node")
      .enable_router (manual_route_router_endpoint)
      .accept_routes_from_channel (
        "manual.api", [&] (zlink::framework::accepted_spot_route_channel_builder_t &routes) {
            routes.connect (manual_route_endpoint);
        });
    manual_route_options.apply ();
    const auto manual_route_spots = manual_route_zlink.spot_nodes ();
    if (manual_route_spots.size () != 1
        || manual_route_spots[0].accepted_route_channels.size () != 1
        || manual_route_spots[0].accepted_route_channels[0].channel_name != "manual.api"
        || manual_route_spots[0].accepted_route_channels[0].manual_connections.size () != 1
        || manual_route_spots[0].accepted_route_channels[0].manual_connections[0]
             != manual_route_endpoint) {
        return 22;
    }

    zlink::framework::zlink_builder_t late_registry_zlink;
    zlink::framework::zlink_framework_options_t late_options (services, handlers, serializers,
                                                              late_registry_zlink, monitoring);
    const auto late_route_endpoint = unique_tcp ("late-route");
    const auto late_router_endpoint = unique_tcp ("late-router");
    const auto late_pub_endpoint = unique_tcp ("late-pub");
    late_options.use_discovery ().add_registry_endpoint ("tcp://registry:5551");
    late_options.add_route_mesh_channel ("late.route")
      .bind (late_route_endpoint)
      .set_routing_id (zlink::routing_id_t::from ("7400"));
    late_options.add_spot_mesh ("late.spots")
      .add_node ("late-node")
      .enable_router (late_router_endpoint)
      .enable_pub_sub (late_pub_endpoint)
      .add_spot<stage_spot_t> ("stage");
    late_options.use_registry_spot_remote_addresses ("late.route");
    late_options.apply ();
    const auto late_spots = late_registry_zlink.spot_nodes ();
    if (late_spots.size () != 1 || late_spots[0].spot_names.size () != 1
        || !late_spots[0].router_bind_endpoint || !late_spots[0].pub_bind_endpoint
        || !late_spots[0].registry_spot_remote_addresses_enabled
        || !late_spots[0].registry_spot_route_channel
        || *late_spots[0].registry_spot_route_channel != "late.route") {
        return 23;
    }

    zlink::context_t native_context;
    zlink::service::registry_t native_registry (native_context);
    zlink::service::discovery_t native_discovery (
      native_context, zlink::auto_connect_type_t::fanout, "remote.play");
    zlink::pub_socket_t native_provider (native_context);
    const auto registry_pub = unique_tcp ("framework-registry-pub");
    const auto registry_router = unique_tcp ("framework-registry-router");
    const auto provider_endpoint = unique_tcp ("framework-registry-provider");
    native_registry.bind (registry_pub, registry_router);
    native_discovery.connect_registry (registry_router);
    native_provider.attach_discovery (native_discovery);
    native_provider.bind (provider_endpoint);

    zlink::framework::registry_query_client_t remote_client;
    auto remote_connected =
      remote_client.connect (zlink::framework::registry_query_client_options_t{registry_router});
    if (!remote_connected) {
        return 28;
    }
    zlink::framework::topology_filter_t remote_filter;
    remote_filter.name = "remote.play";
    remote_filter.kind = zlink::framework::service_kind_t::channel;
    remote_filter.role = zlink::framework::service_role_t::publisher;

    bool remote_found = false;
    const auto deadline = std::chrono::steady_clock::now () + 5s;
    while (std::chrono::steady_clock::now () < deadline && !remote_found) {
        auto remote_topology = remote_client.topology (remote_filter);
        if (remote_topology) {
            for (const auto &entry : remote_topology.value ()) {
                if (entry.name == "remote.play" && entry.endpoint == provider_endpoint
                    && entry.source == zlink::framework::topology_source_t::remote) {
                    remote_found = true;
                    break;
                }
            }
        }
        if (!remote_found) {
            std::this_thread::sleep_for (25ms);
        }
    }
    remote_client.close ();
    if (!remote_found) {
        return 29;
    }

    return 0;
}
