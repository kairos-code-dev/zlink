/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include <zlink/locations/redis.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <sw/redis++/redis++.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace
{

using zlink::framework::actor_location_t;
using zlink::framework::actor_location_key_t;
using zlink::framework::actor_ref_t;
using zlink::framework::location_auto_connect_type_t;
using zlink::framework::location_kind_t;
using zlink::framework::location_owner_token_t;
using zlink::framework::location_role_t;
using zlink::framework::node_rid_t;
using zlink::framework::peer_location_filter_t;
using zlink::framework::peer_location_key_t;
using zlink::framework::peer_location_t;
using zlink::framework::route_kind_t;
using zlink::framework::route_location_key_t;
using zlink::framework::route_location_t;
using zlink::framework::spot_location_key_t;
using zlink::framework::spot_location_t;
using zlink::framework::location_write_status_t;
using zlink::framework::locations::redis::detail::redis_location_key_schema_t;
using zlink::framework::locations::redis::detail::redis_location_row_codec_t;
using zlink::framework::locations::redis::detail::redis_location_script_result_t;
using zlink::framework::locations::redis::detail::redis_location_scripts_t;
using zlink::framework::locations::redis::redis_location_options_t;
using zlink::framework::locations::redis::redis_location_store_t;

std::string unique_prefix ()
{
    const auto now = std::chrono::steady_clock::now ().time_since_epoch ().count ();
    return "zlink:cpp:redis-test:" + std::to_string (now);
}

std::vector<std::string> redis_test_endpoints ()
{
    std::vector<std::string> endpoints;
    if (const auto *env = std::getenv ("ZLINK_REDIS_TEST_ENDPOINT")) {
        endpoints.emplace_back (env);
    }
    endpoints.emplace_back ("tcp://127.0.0.1:16379");
    endpoints.emplace_back ("tcp://127.0.0.1:6379");
    return endpoints;
}

std::optional<redis_location_options_t> find_redis_options ()
{
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
    for (const auto &endpoint : redis_test_endpoints ()) {
        redis_location_options_t options{.connection_string = endpoint,
                                         .key_prefix = unique_prefix ()};
        redis_location_store_t store (options);
        auto probe = store.renew_owner_lease ("probe-owner", zlink::routing_id_t::from ("probe"),
                                              std::chrono::milliseconds (250));
        if (probe.result ().has_value ()) {
            auto cleanup = store.remove_owner_lease ("probe-owner");
            (void) cleanup;
            return options;
        }
    }
#endif
    return std::nullopt;
}

std::optional<redis_location_options_t> cross_language_options (std::string language)
{
    const auto *prefix = std::getenv ("ZLINK_REDIS_CROSS_LANGUAGE_PREFIX");
    if (prefix == nullptr || std::string (prefix).empty ()) {
        return std::nullopt;
    }
    auto options = find_redis_options ();
    if (!options) {
        return std::nullopt;
    }
    options->key_prefix = std::string (prefix) + ":" + std::move (language);
    return options;
}

nlohmann::json read_redis_location_fixture ()
{
    std::vector<std::filesystem::path> candidates;
    auto current = std::filesystem::current_path ();
    for (int i = 0; i < 8; ++i) {
        candidates.push_back (
          current / "framework/testdata/location/redis/actor-location-v2.json");
        candidates.push_back (current / "testdata/location/redis/actor-location-v2.json");
        candidates.push_back (current / "../../testdata/location/redis/actor-location-v2.json");
        current = current.parent_path ();
    }
    for (const auto &candidate : candidates) {
        std::ifstream input (candidate);
        if (input) {
            return nlohmann::json::parse (input);
        }
    }
    throw std::runtime_error ("actor-location-v2.json fixture was not found");
}

const nlohmann::json &fixture_row (const nlohmann::json &fixture, std::string_view kind)
{
    for (const auto &row : fixture.at ("rows")) {
        if (row.at ("kind").get<std::string> () == kind) {
            return row;
        }
    }
    throw std::runtime_error ("fixture row was not found");
}

TEST (ZLinkFrameworkLocationsRedis, StoreTypeExposesUnifiedLocationContracts)
{
    redis_location_store_t store (redis_location_options_t{
      .connection_string = "tcp://127.0.0.1:6379", .key_prefix = "zlink:test"});

    zlink::framework::location_store_t *location_store = &store;
    zlink::framework::peer_location_store_t *peer_store = &store;
    zlink::framework::spot_location_store_t *spot_store = &store;
    zlink::framework::actor_location_store_t *actor_store = &store;
    zlink::framework::route_location_store_t *route_store = &store;
    zlink::framework::owner_lease_store_t *lease_store = &store;
    zlink::framework::location_change_stamp_store_t *stamp_store = &store;

    EXPECT_NE (nullptr, location_store);
    EXPECT_NE (nullptr, peer_store);
    EXPECT_NE (nullptr, spot_store);
    EXPECT_NE (nullptr, actor_store);
    EXPECT_NE (nullptr, route_store);
    EXPECT_NE (nullptr, lease_store);
    EXPECT_NE (nullptr, stamp_store);
    EXPECT_EQ ("zlink:test", store.options ().key_prefix);
}

TEST (ZLinkFrameworkLocationsRedis, StoreWithoutRedisClientReportsUnavailable)
{
    redis_location_store_t store (redis_location_options_t{
      .connection_string = "tcp://127.0.0.1:1", .key_prefix = "zlink:test"});

    auto claim = store.update_actor (
      actor_location_t{.actor_id = "alice",
                       .actor_type = "player",
                       .actor_ref = std::nullopt,
                       .node_rid = zlink::routing_id_t::from ("node-a"),
                       .spot_mesh_name = "play",
                       .owner_id = "owner-a"},
      zlink::framework::location_write_intent_t::new_claim);
    EXPECT_FALSE (claim.result ().has_value ());
    ASSERT_NE (nullptr, claim.result ().error ());
    EXPECT_TRUE (claim.result ().error ()->is_retriable ());

    auto lease = store.renew_owner_lease ("owner-a", zlink::routing_id_t::from ("node-a"),
                                          std::chrono::milliseconds (500));
    EXPECT_FALSE (lease.result ().has_value ());
    ASSERT_NE (nullptr, lease.result ().error ());
    EXPECT_TRUE (lease.result ().error ()->is_retriable ());

    auto peer = store.update_peer (
      peer_location_t{.auto_connect_type = location_auto_connect_type_t::client_server,
                      .mesh_name = "api",
                      .role = location_role_t::dealer,
                      .endpoint = "tcp://127.0.0.1:7777",
                      .owner_id = "owner-a"},
      zlink::framework::location_write_intent_t::new_claim);
    EXPECT_FALSE (peer.result ().has_value ());
    ASSERT_NE (nullptr, peer.result ().error ());
    EXPECT_TRUE (peer.result ().error ()->is_retriable ());

    auto spot = store.update_spot (
      spot_location_t{.mesh_name = "play",
                      .spot_rid = zlink::routing_id_t::from ("spot-a"),
                      .spot_type = "room",
                      .node_rid = zlink::routing_id_t::from ("node-a"),
                      .owner_id = "owner-a"},
      zlink::framework::location_write_intent_t::new_claim);
    EXPECT_FALSE (spot.result ().has_value ());
    ASSERT_NE (nullptr, spot.result ().error ());
    EXPECT_TRUE (spot.result ().error ()->is_retriable ());

    auto route = store.update_route (
      route_location_t{.route_kind = route_kind_t::spot_name,
                       .route_key = "room-a",
                       .owner_node_rid = zlink::routing_id_t::from ("node-a"),
                       .owner_id = "owner-a",
                       .value = {1, 2, 3}},
      zlink::framework::location_write_intent_t::new_claim);
    EXPECT_FALSE (route.result ().has_value ());
    ASSERT_NE (nullptr, route.result ().error ());
    EXPECT_TRUE (route.result ().error ()->is_retriable ());

    auto read = store.resolve_actor (actor_location_key_t{.actor_id = "alice"});
    EXPECT_FALSE (read.result ().has_value ());
    ASSERT_NE (nullptr, read.result ().error ());
    EXPECT_TRUE (read.result ().error ()->is_retriable ());

    auto read_spot = store.resolve_spot (spot_location_key_t{
      .mesh_name = "play", .spot_rid = zlink::routing_id_t::from ("spot-a")});
    EXPECT_FALSE (read_spot.result ().has_value ());
    ASSERT_NE (nullptr, read_spot.result ().error ());
    EXPECT_TRUE (read_spot.result ().error ()->is_retriable ());

    auto read_route = store.resolve_route (
      route_location_key_t{.route_kind = route_kind_t::spot_name, .route_key = "room-a"});
    EXPECT_FALSE (read_route.result ().has_value ());
    ASSERT_NE (nullptr, read_route.result ().error ());
    EXPECT_TRUE (read_route.result ().error ()->is_retriable ());

    auto list = store.list_actors (zlink::framework::actor_location_filter_t{},
                                   zlink::framework::location_page_request_t{.page_size = 2});
    EXPECT_FALSE (list.result ().has_value ());
    ASSERT_NE (nullptr, list.result ().error ());
    EXPECT_TRUE (list.result ().error ()->is_retriable ());

    auto peers = store.list_peers (zlink::framework::peer_location_filter_t{});
    EXPECT_FALSE (peers.result ().has_value ());
    ASSERT_NE (nullptr, peers.result ().error ());
    EXPECT_TRUE (peers.result ().error ()->is_retriable ());

    auto spots = store.list_spots (zlink::framework::spot_location_filter_t{},
                                   zlink::framework::location_page_request_t{.page_size = 2});
    EXPECT_FALSE (spots.result ().has_value ());
    ASSERT_NE (nullptr, spots.result ().error ());
    EXPECT_TRUE (spots.result ().error ()->is_retriable ());

    auto routes = store.list_routes (zlink::framework::route_location_filter_t{},
                                     zlink::framework::location_page_request_t{.page_size = 2});
    EXPECT_FALSE (routes.result ().has_value ());
    ASSERT_NE (nullptr, routes.result ().error ());
    EXPECT_TRUE (routes.result ().error ()->is_retriable ());

    auto removed_peer = store.remove_peer (
      peer_location_key_t{.auto_connect_type = location_auto_connect_type_t::client_server,
                          .mesh_name = "api",
                          .role = location_role_t::dealer,
                          .endpoint = "tcp://127.0.0.1:7777"},
      location_owner_token_t{"owner-a", 0});
    EXPECT_FALSE (removed_peer.result ().has_value ());
    ASSERT_NE (nullptr, removed_peer.result ().error ());
    EXPECT_TRUE (removed_peer.result ().error ()->is_retriable ());

    auto removed_spot = store.remove_spot (
      spot_location_key_t{.mesh_name = "play", .spot_rid = zlink::routing_id_t::from ("spot-a")},
      location_owner_token_t{"owner-a", 0});
    EXPECT_FALSE (removed_spot.result ().has_value ());
    ASSERT_NE (nullptr, removed_spot.result ().error ());
    EXPECT_TRUE (removed_spot.result ().error ()->is_retriable ());

    auto removed_route = store.remove_route (
      route_location_key_t{.route_kind = route_kind_t::spot_name, .route_key = "room-a"},
      location_owner_token_t{"owner-a", 0});
    EXPECT_FALSE (removed_route.result ().has_value ());
    ASSERT_NE (nullptr, removed_route.result ().error ());
    EXPECT_TRUE (removed_route.result ().error ()->is_retriable ());

    auto removed_actor = store.remove_actor (
      actor_location_key_t{.actor_id = "alice"},
      location_owner_token_t{"owner-a", 0});
    EXPECT_FALSE (removed_actor.result ().has_value ());
    ASSERT_NE (nullptr, removed_actor.result ().error ());
    EXPECT_TRUE (removed_actor.result ().error ()->is_retriable ());

    auto removed = store.remove_all_by_owner ("owner-a");
    EXPECT_FALSE (removed.result ().has_value ());
    ASSERT_NE (nullptr, removed.result ().error ());
    EXPECT_TRUE (removed.result ().error ()->is_retriable ());

    auto removed_lease = store.remove_owner_lease ("owner-a");
    EXPECT_FALSE (removed_lease.result ().has_value ());
    ASSERT_NE (nullptr, removed_lease.result ().error ());
    EXPECT_TRUE (removed_lease.result ().error ()->is_retriable ());

    auto leases = store.list_owner_leases ();
    EXPECT_FALSE (leases.result ().has_value ());
    ASSERT_NE (nullptr, leases.result ().error ());
    EXPECT_TRUE (leases.result ().error ()->is_retriable ());

    auto stamp = store.get_change_stamp (
      zlink::framework::location_change_stamp_scope_t{.kind = location_kind_t::spot,
                                                      .mesh_name = "play"});
    EXPECT_FALSE (stamp.result ().has_value ());
    ASSERT_NE (nullptr, stamp.result ().error ());
    EXPECT_TRUE (stamp.result ().error ()->is_retriable ());
}

TEST (ZLinkFrameworkLocationsRedis, RedisServerRoundTripUsesStoreSchema)
{
#if !defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
    GTEST_SKIP () << "redis-plus-plus is not available in this build";
#else
    const auto options = find_redis_options ();
    if (!options) {
        GTEST_SKIP () << "Redis is not reachable; set ZLINK_REDIS_TEST_ENDPOINT to enable";
    }

    redis_location_store_t store (*options);
    const auto lease =
      store
        .renew_owner_lease ("owner-a", zlink::routing_id_t::from ("node-a"),
                            std::chrono::seconds (5))
        .result ()
        .value ();
    EXPECT_GT (lease.lease_expires_at, lease.store_now);

    const auto before =
      store.get_change_stamp ({.kind = location_kind_t::actor, .mesh_name = std::nullopt})
        .result ()
        .value ();
    auto claim = store.update_actor (
      actor_location_t{.actor_id = "alice",
                       .actor_type = "player",
                       .actor_ref = std::nullopt,
                       .node_rid = zlink::routing_id_t::from ("node-a"),
                       .spot_mesh_name = "play",
                       .owner_id = "owner-a"},
      zlink::framework::location_write_intent_t::new_claim);
    ASSERT_EQ (location_write_status_t::stored, claim.result ().value ().status);
    ASSERT_GT (claim.result ().value ().generation, 0);

    sw::redis::Redis redis (options->connection_string);
    const auto actor_hash_key = redis_location_key_schema_t::row_key (
      options->key_prefix, location_kind_t::actor,
      redis_location_key_schema_t::encode_actor_key (actor_location_key_t{.actor_id = "alice"}));
    std::vector<std::string> actor_hash_fields;
    redis.hkeys (actor_hash_key, std::back_inserter (actor_hash_fields));
    EXPECT_EQ (4u, actor_hash_fields.size ());
    EXPECT_EQ (actor_hash_fields.end (),
               std::find (actor_hash_fields.begin (), actor_hash_fields.end (), "mesh"));

    const auto after =
      store.get_change_stamp ({.kind = location_kind_t::actor, .mesh_name = std::nullopt})
        .result ()
        .value ();
    EXPECT_GT (after, before);

    auto resolved = store.resolve_actor (actor_location_key_t{.actor_id = "alice"});
    ASSERT_TRUE (resolved.result ().has_value ());
    ASSERT_TRUE (resolved.result ().value ().has_value ());
    EXPECT_EQ ("alice", resolved.result ().value ()->actor_id);
    EXPECT_EQ (claim.result ().value ().generation, resolved.result ().value ()->generation);
    EXPECT_EQ ("node-a", resolved.result ().value ()->node_rid.to_string ());

    auto page = store.list_actors (zlink::framework::actor_location_filter_t{.actor_type = "player"},
                                   zlink::framework::location_page_request_t{.page_size = 10});
    ASSERT_TRUE (page.result ().has_value ());
    ASSERT_EQ (1u, page.result ().value ().items.size ());

    ASSERT_TRUE (store
                   .renew_owner_lease ("publisher-owner", zlink::routing_id_t::from ("pub-a"),
                                       std::chrono::seconds (5))
                   .result ()
                   .has_value ());
    auto peer_claim = store.update_peer (
      peer_location_t{.auto_connect_type = location_auto_connect_type_t::fanout,
                      .mesh_name = "pubsub.events",
                      .role = location_role_t::pub,
                      .endpoint = "tcp://127.0.0.1:7007",
                      .weight = 100,
                      .owner_id = "publisher-owner"},
      zlink::framework::location_write_intent_t::new_claim);
    ASSERT_EQ (location_write_status_t::stored, peer_claim.result ().value ().status);
    auto peers = store.list_peers (
      zlink::framework::peer_location_filter_t{.auto_connect_type =
                                                 location_auto_connect_type_t::fanout,
                                               .mesh_name = "pubsub.events"});
    ASSERT_TRUE (peers.result ().has_value ());
    ASSERT_EQ (1u, peers.result ().value ().size ());
    EXPECT_EQ (location_role_t::pub, peers.result ().value ()[0].role);
    EXPECT_EQ ("tcp://127.0.0.1:7007", peers.result ().value ()[0].endpoint);

    auto leases = store.list_owner_leases ();
    ASSERT_TRUE (leases.result ().has_value ());
    const auto owner_a =
      std::find_if (leases.result ().value ().leases.begin (),
                    leases.result ().value ().leases.end (),
                    [] (const auto &lease) { return lease.owner_id == "owner-a"; });
    ASSERT_NE (leases.result ().value ().leases.end (), owner_a);
    EXPECT_EQ ("node-a", owner_a->node_rid.to_string ());
    EXPECT_GT (owner_a->lease_expires_at, leases.result ().value ().store_now);

    auto removed = store.remove_all_by_owner ("owner-a");
    ASSERT_TRUE (removed.result ().has_value ());
    EXPECT_EQ (1, removed.result ().value ());
    auto missing = store.resolve_actor (actor_location_key_t{.actor_id = "alice"});
    ASSERT_TRUE (missing.result ().has_value ());
    EXPECT_FALSE (missing.result ().value ().has_value ());
    (void) store.remove_all_by_owner ("publisher-owner");
    (void) store.remove_owner_lease ("publisher-owner");
    (void) store.remove_owner_lease ("owner-a");
#endif
}

TEST (ZLinkFrameworkLocationsRedis, CrossLanguageWritesRowsForDotnetToRead)
{
#if !defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
    GTEST_SKIP () << "redis-plus-plus is not available in this build";
#else
    const auto options = cross_language_options ("cpp");
    if (!options) {
        GTEST_SKIP () << "Redis cross-language prefix or Redis endpoint is not available";
    }

    redis_location_store_t store (*options);
    ASSERT_TRUE (store
                   .renew_owner_lease ("cpp-owner", zlink::routing_id_t::from ("cpp-node"),
                                       std::chrono::seconds (30))
                   .result ()
                   .has_value ());
    EXPECT_EQ (location_write_status_t::stored,
               store.update_peer (
                      peer_location_t{.auto_connect_type = location_auto_connect_type_t::route_mesh,
                                      .mesh_name = "cross",
                                      .node_rid = zlink::routing_id_t::from ("cpp-node"),
                                      .role = location_role_t::router,
                                      .endpoint = "tcp://127.0.0.1:5330",
                                      .weight = 100,
                                      .value = 7,
                                      .metadata = {{"route-endpoint", "tcp://127.0.0.1:6330"}},
                                      .capabilities = {"cpp", "route"},
                                      .owner_id = "cpp-owner"},
                      zlink::framework::location_write_intent_t::new_claim)
                 .result ()
                 .value ()
                 .status);
    EXPECT_EQ (location_write_status_t::stored,
               store.update_spot (
                      spot_location_t{.mesh_name = "cross",
                                      .spot_rid = zlink::routing_id_t::from ("cpp-spot"),
                                      .spot_type = "cpp-game",
                                      .node_rid = zlink::routing_id_t::from ("cpp-node"),
                                      .spot_kind = zlink::spot_kind::user,
                                      .route_endpoint = "tcp://127.0.0.1:5330",
                                      .owner_id = "cpp-owner"},
                      zlink::framework::location_write_intent_t::new_claim)
                 .result ()
                 .value ()
                 .status);
    EXPECT_EQ (location_write_status_t::stored,
               store.update_actor (
                      actor_location_t{.actor_id = "cpp-actor",
                       .actor_type = "player",
                                       .actor_ref = std::nullopt,
                                       .node_rid = zlink::routing_id_t::from ("cpp-node"),
                                       .location_kind = zlink::spot_kind::user,
                                       .spot_mesh_name = "mesh",
                                       .spot_rid = zlink::routing_id_t::from ("cpp-spot"),
                                       .owner_id = "cpp-owner"},
                      zlink::framework::location_write_intent_t::new_claim)
                 .result ()
                 .value ()
                 .status);
    EXPECT_EQ (location_write_status_t::stored,
               store.update_route (
                      route_location_t{.route_kind = route_kind_t::actor_session,
                                       .route_key = "cpp-route",
                                       .owner_node_rid = zlink::routing_id_t::from ("cpp-node"),
                                       .owner_id = "cpp-owner",
                                       .value = {10, 11, 12, 13}},
                      zlink::framework::location_write_intent_t::new_claim)
                 .result ()
                 .value ()
                 .status);
#endif
}

TEST (ZLinkFrameworkLocationsRedis, CrossLanguageReadsDotnetRows)
{
#if !defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
    GTEST_SKIP () << "redis-plus-plus is not available in this build";
#else
    const auto options = cross_language_options ("dotnet");
    if (!options) {
        GTEST_SKIP () << "Redis cross-language prefix or Redis endpoint is not available";
    }

    redis_location_store_t store (*options);
    auto actor = store.resolve_actor (actor_location_key_t{.actor_id = "dotnet-actor"})
                   .result ();
    ASSERT_TRUE (actor.has_value ());
    ASSERT_TRUE (actor.value ().has_value ());
    ASSERT_TRUE (actor.value ()->actor_ref.has_value ());
    EXPECT_EQ ("dotnet-actor", actor.value ()->actor_ref->actor_id ());
    EXPECT_EQ ("dotnet-node", actor.value ()->actor_ref->node_rid ().value ());
    EXPECT_EQ (1u, actor.value ()->actor_ref->generation ());
    EXPECT_EQ ("dotnet-node", actor.value ()->node_rid.to_string ());
    EXPECT_EQ ("dotnet-owner", actor.value ()->owner_id);

    auto spot = store.resolve_spot (spot_location_key_t{
                                      .mesh_name = "cross",
                                      .spot_rid = zlink::routing_id_t::from ("dotnet-spot")})
                  .result ();
    ASSERT_TRUE (spot.has_value ());
    ASSERT_TRUE (spot.value ().has_value ());
    ASSERT_TRUE (spot.value ()->spot_type.has_value ());
    EXPECT_EQ ("dotnet-game", *spot.value ()->spot_type);
    EXPECT_EQ ("dotnet-node", spot.value ()->node_rid.to_string ());

    auto route =
      store
        .resolve_route (
          route_location_key_t{.route_kind = route_kind_t::actor_session, .route_key = "dotnet-route"})
        .result ();
    ASSERT_TRUE (route.has_value ());
    ASSERT_TRUE (route.value ().has_value ());
    EXPECT_EQ ((std::vector<std::uint8_t>{9, 8, 7, 6}), route.value ()->value);

    auto peers = store
                   .list_peers (peer_location_filter_t{
                     .auto_connect_type = location_auto_connect_type_t::route_mesh,
                     .mesh_name = "cross",
                     .role = location_role_t::router,
                     .node_rid = zlink::routing_id_t::from ("dotnet-node")})
                   .result ();
    ASSERT_TRUE (peers.has_value ());
    const auto peer = std::find_if (peers.value ().begin (), peers.value ().end (),
                                    [] (const auto &row) {
                                        return row.endpoint == "tcp://127.0.0.1:5310";
                                    });
    ASSERT_NE (peers.value ().end (), peer);
    EXPECT_EQ ("tcp://127.0.0.1:6310", peer->metadata.at ("route-endpoint"));
    EXPECT_EQ ((std::vector<std::string>{"dotnet", "route"}), peer->capabilities);
#endif
}

TEST (ZLinkFrameworkLocationsRedis, LuaScriptsPreserveDotnetAtomicStoreContract)
{
    const auto write = std::string (redis_location_scripts_t::write);
    EXPECT_NE (std::string::npos, write.find ("redis.call('TIME')"));
    EXPECT_NE (std::string::npos, write.find ("redis.call('EXISTS', ARGV[6] .. currentOwner)"));
    EXPECT_NE (std::string::npos, write.find ("return {'conflict', 0, nowMs}"));
    EXPECT_NE (std::string::npos, write.find ("local gen = redis.call('INCR', KEYS[2])"));
    EXPECT_NE (std::string::npos, write.find ("'owner', owner, 'gen', gen, 'json', ARGV[4]"));
    EXPECT_NE (std::string::npos, write.find ("redis.call('SADD', ARGV[7] .. owner, ARGV[5])"));
    EXPECT_NE (std::string::npos, write.find ("tonumber(redis.call('HGET', KEYS[1], 'gen'))"));
    EXPECT_NE (std::string::npos, write.find ("return {'stale', 0, nowMs}"));

    const auto remove = std::string (redis_location_scripts_t::remove);
    EXPECT_NE (std::string::npos, remove.find ("redis.call('DEL', KEYS[1])"));
    EXPECT_NE (std::string::npos, remove.find ("redis.call('SREM', KEYS[2], ARGV[3])"));
    EXPECT_NE (std::string::npos, remove.find ("redis.call('SREM', ARGV[4] .. currentOwner, ARGV[3])"));
    EXPECT_EQ (std::string::npos, remove.find ("DEL', KEYS[2]"));

    const auto remove_by_owner = std::string (redis_location_scripts_t::remove_by_owner);
    EXPECT_NE (std::string::npos, remove_by_owner.find ("for i = 1, 4 do"));
    EXPECT_NE (std::string::npos, remove_by_owner.find ("redis.call('SMEMBERS', ownerKey)"));
    EXPECT_NE (std::string::npos, remove_by_owner.find ("local mesh = redis.call('HGET', rowHash, 'mesh')"));
    EXPECT_NE (std::string::npos, remove_by_owner.find ("redis.call('INCR', stampBase .. ':' .. mesh)"));
    EXPECT_NE (std::string::npos, remove_by_owner.find ("redis.call('DEL', ownerKey)"));

    const auto renew_lease = std::string (redis_location_scripts_t::renew_lease);
    EXPECT_NE (std::string::npos, renew_lease.find ("'PX', ARGV[3]"));
    EXPECT_NE (std::string::npos, renew_lease.find ("ARGV[2] .. '|' .. nowMs"));
    EXPECT_NE (std::string::npos, renew_lease.find ("redis.call('SADD', KEYS[2], ARGV[1])"));

    const auto list_leases = std::string (redis_location_scripts_t::list_leases);
    EXPECT_NE (std::string::npos, list_leases.find ("redis.call('PTTL', leaseKey)"));
    EXPECT_NE (std::string::npos, list_leases.find ("redis.call('SREM', KEYS[1], ownerId)"));
    EXPECT_NE (std::string::npos, list_leases.find ("return {nowMs, out}"));
}

TEST (ZLinkFrameworkLocationsRedis, ScriptWriteResultMapsRedisStatuses)
{
    const auto stored = redis_location_script_result_t::write_result ("stored", 42, 1000);
    EXPECT_EQ (location_write_status_t::stored, stored.status);
    EXPECT_EQ (42, stored.generation);
    EXPECT_EQ (std::chrono::system_clock::time_point (std::chrono::milliseconds (1000)),
               stored.updated_at);

    const auto conflict = redis_location_script_result_t::write_result ("conflict", 9, 1000);
    EXPECT_EQ (location_write_status_t::rejected_conflict, conflict.status);
    EXPECT_EQ (0, conflict.generation);

    const auto stale = redis_location_script_result_t::write_result ("stale", 9, 1000);
    EXPECT_EQ (location_write_status_t::ignored_stale, stale.status);
    EXPECT_EQ (0, stale.generation);
}

TEST (ZLinkFrameworkLocationsRedis, PhysicalKeysUseCommonRedisSchema)
{
    const auto peer_key = redis_location_key_schema_t::encode_peer_key (
      peer_location_key_t{.auto_connect_type = location_auto_connect_type_t::client_server,
                          .mesh_name = "api-mesh",
                          .role = location_role_t::dealer,
                          .node_rid = std::nullopt,
                          .endpoint = "tcp://127.0.0.1"});
    EXPECT_EQ ("13:client-server8:api-mesh6:dealer15:tcp://127.0.0.1", peer_key);
    EXPECT_EQ ("zlink:test:row:peer:" + peer_key,
               redis_location_key_schema_t::row_key ("zlink:test", location_kind_t::peer,
                                                     peer_key));
    EXPECT_EQ ("zlink:test:gen:peer:" + peer_key,
               redis_location_key_schema_t::generation_key ("zlink:test", location_kind_t::peer,
                                                            peer_key));
    EXPECT_EQ ("zlink:test:keys:peer",
               redis_location_key_schema_t::keys_key ("zlink:test", location_kind_t::peer));
    EXPECT_EQ ("zlink:test:own:peer:owner-a",
               redis_location_key_schema_t::owner_key ("zlink:test", location_kind_t::peer,
                                                       "owner-a"));
    EXPECT_EQ ("zlink:test:lease:owner-a",
               redis_location_key_schema_t::lease_key ("zlink:test", "owner-a"));
    EXPECT_EQ ("zlink:test:leases", redis_location_key_schema_t::leases_key ("zlink:test"));
    EXPECT_EQ ("zlink:test:stamp:spot:mesh-a",
               redis_location_key_schema_t::stamp_key ("zlink:test", location_kind_t::spot,
                                                       std::string_view ("mesh-a")));
}

TEST (ZLinkFrameworkLocationsRedis, RowKeysUseDotnetCanonicalKeyBytes)
{
    EXPECT_EQ ("9:mesh-main12:73706f742d61",
               redis_location_key_schema_t::encode_spot_key (
                 spot_location_key_t{.mesh_name = "mesh-main",
                                     .spot_rid = zlink::routing_id_t::from ("spot-a")}));
    EXPECT_EQ ("7:actor-1",
               redis_location_key_schema_t::encode_actor_key (
                 actor_location_key_t{.actor_id = "actor-1"}));
    EXPECT_EQ ("1:113:session:alpha",
               redis_location_key_schema_t::encode_route_key (
                 route_location_key_t{.route_kind = route_kind_t::actor_session,
                                      .route_key = "session:alpha"}));
}

TEST (ZLinkFrameworkLocationsRedis, RowCodecMatchesCommonFixtureBytes)
{
    const auto fixture = read_redis_location_fixture ();

    const auto &actor = fixture_row (fixture, "actor");
    const auto actor_row =
      actor_location_t{.actor_id = "actor-1",
                       .actor_type = "player",
                       .actor_ref = actor_ref_t (node_rid_t::from_string ("node-1"), "player",
                                                 "actor-1", 1),
                       .node_rid = zlink::routing_id_t::from ("node-1"),
                       .location_kind = zlink::spot_kind::entry,
                       .spot_mesh_name = "play",
                       .spot_rid = std::nullopt,
                       .owner_id = "owner-a",
                       .generation = 0};
    EXPECT_EQ (actor.at ("key").get<std::string> (),
               redis_location_key_schema_t::encode_actor_key (
                 actor_location_key_t{.actor_id = actor_row.actor_id}));
    EXPECT_EQ (actor.at ("hash").at ("json").get<std::string> (),
               redis_location_row_codec_t::encode_actor (actor_row));
    const auto decoded_actor =
      redis_location_row_codec_t::decode_actor (actor.at ("hash").at ("json").get<std::string> ());
    ASSERT_TRUE (decoded_actor.actor_ref.has_value ());
    EXPECT_EQ ("node-1", decoded_actor.actor_ref->node_rid ().value ());
    EXPECT_EQ ("actor-1", decoded_actor.actor_ref->actor_id ());
    EXPECT_EQ (1u, decoded_actor.actor_ref->generation ());

    const auto &peer = fixture_row (fixture, "peer");
    const auto peer_row =
      peer_location_t{.auto_connect_type = location_auto_connect_type_t::route_mesh,
                      .mesh_name = "play",
                      .node_rid = zlink::routing_id_t::from ("node-1"),
                      .role = location_role_t::router,
                      .endpoint = "tcp://127.0.0.1:5001",
                      .weight = 100,
                      .value = 7,
                      .metadata = {{"route-endpoint", "tcp://127.0.0.1:6001"}},
                      .capabilities = {"router", "route-bridge"},
                      .owner_id = "owner-a",
                      .generation = 0};
    EXPECT_EQ (peer.at ("key").get<std::string> (),
               redis_location_key_schema_t::encode_peer_key (
                 peer_location_key_t{.auto_connect_type = peer_row.auto_connect_type,
                                     .mesh_name = peer_row.mesh_name,
                                     .role = peer_row.role,
                                     .node_rid = peer_row.node_rid,
                                     .endpoint = peer_row.endpoint}));
    EXPECT_EQ (peer.at ("hash").at ("json").get<std::string> (),
               redis_location_row_codec_t::encode_peer (peer_row));

    const auto &spot = fixture_row (fixture, "spot");
    const auto spot_row =
      spot_location_t{.mesh_name = "play",
                      .spot_rid = zlink::routing_id_t::from ("spot-1"),
                      .spot_type = "game",
                      .node_rid = zlink::routing_id_t::from ("node-1"),
                      .spot_kind = zlink::spot_kind::user,
                      .route_endpoint = std::nullopt,
                      .owner_id = "owner-a",
                      .generation = 0};
    EXPECT_EQ (spot.at ("key").get<std::string> (),
               redis_location_key_schema_t::encode_spot_key (
                 spot_location_key_t{.mesh_name = spot_row.mesh_name,
                                     .spot_rid = spot_row.spot_rid}));
    EXPECT_EQ (spot.at ("hash").at ("json").get<std::string> (),
               redis_location_row_codec_t::encode_spot (spot_row));

    const auto &route = fixture_row (fixture, "route");
    const auto route_row =
      route_location_t{.route_kind = route_kind_t::actor_session,
                       .route_key = "route-1",
                       .owner_node_rid = zlink::routing_id_t::from ("node-1"),
                       .owner_id = "owner-a",
                       .generation = 0,
                       .value = {1, 2, 3, 4}};
    EXPECT_EQ (route.at ("key").get<std::string> (),
               redis_location_key_schema_t::encode_route_key (
                 route_location_key_t{.route_kind = route_row.route_kind,
                                      .route_key = route_row.route_key}));
    EXPECT_EQ (route.at ("hash").at ("json").get<std::string> (),
               redis_location_row_codec_t::encode_route (route_row));
}

TEST (ZLinkFrameworkLocationsRedis, PeerRowJsonUsesDotnetFieldSchema)
{
    const auto encoded = redis_location_row_codec_t::encode_peer (
      peer_location_t{.auto_connect_type = location_auto_connect_type_t::route_mesh,
                      .mesh_name = "play",
                      .node_rid = zlink::routing_id_t::from ("node-a"),
                      .role = location_role_t::router,
                      .endpoint = "tcp://127.0.0.1:7001",
                      .weight = 3,
                      .value = 17,
                      .metadata = {{"zone", "a"}},
                      .capabilities = {"spot-route"},
                      .owner_id = "owner-a",
                      .generation = 9});

    const auto json = nlohmann::json::parse (encoded);
    EXPECT_EQ (static_cast<int> (location_auto_connect_type_t::route_mesh),
               json.at ("AutoConnectType").get<int> ());
    EXPECT_EQ ("play", json.at ("MeshName").get<std::string> ());
    EXPECT_EQ (zlink::routing_id_t::from ("node-a").to_hex (),
               json.at ("NodeRid").get<std::string> ());
    EXPECT_EQ (static_cast<int> (location_role_t::router), json.at ("Role").get<int> ());
    EXPECT_EQ ("tcp://127.0.0.1:7001", json.at ("Endpoint").get<std::string> ());
    EXPECT_EQ ("a", json.at ("Metadata").at ("zone").get<std::string> ());
    EXPECT_EQ ("spot-route", json.at ("Capabilities").at (0).get<std::string> ());

    const auto decoded = redis_location_row_codec_t::decode_peer (encoded);
    EXPECT_EQ (location_auto_connect_type_t::route_mesh, decoded.auto_connect_type);
    ASSERT_TRUE (decoded.node_rid.has_value ());
    EXPECT_EQ ("node-a", decoded.node_rid->to_string ());
    EXPECT_EQ (location_role_t::router, decoded.role);
    EXPECT_EQ (17, decoded.value);
}

TEST (ZLinkFrameworkLocationsRedis, SpotRowJsonUsesDotnetFieldSchema)
{
    const auto encoded = redis_location_row_codec_t::encode_spot (
      spot_location_t{.mesh_name = "play",
                      .spot_rid = zlink::routing_id_t::from ("spot-a"),
                      .spot_type = "room",
                      .node_rid = zlink::routing_id_t::from ("node-a"),
                      .spot_kind = zlink::spot_kind::user,
                      .route_endpoint = "tcp://127.0.0.1:7001",
                      .owner_id = "owner-a",
                      .generation = 6});

    const auto json = nlohmann::json::parse (encoded);
    EXPECT_EQ ("play", json.at ("MeshName").get<std::string> ());
    EXPECT_EQ (zlink::routing_id_t::from ("spot-a").to_hex (),
               json.at ("SpotRid").get<std::string> ());
    EXPECT_EQ ("room", json.at ("SpotType").get<std::string> ());
    EXPECT_EQ (zlink::routing_id_t::from ("node-a").to_hex (),
               json.at ("NodeRid").get<std::string> ());
    EXPECT_EQ (static_cast<int> (zlink::spot_kind::user), json.at ("SpotKind").get<int> ());
    EXPECT_EQ ("tcp://127.0.0.1:7001", json.at ("RouteEndpoint").get<std::string> ());

    const auto decoded = redis_location_row_codec_t::decode_spot (encoded);
    EXPECT_EQ ("play", decoded.mesh_name);
    EXPECT_EQ ("spot-a", decoded.spot_rid.to_string ());
    ASSERT_TRUE (decoded.spot_type.has_value ());
    EXPECT_EQ ("room", *decoded.spot_type);
    EXPECT_EQ (zlink::spot_kind::user, decoded.spot_kind);
}

TEST (ZLinkFrameworkLocationsRedis, ActorRowJsonUsesDotnetFieldSchema)
{
    const auto encoded = redis_location_row_codec_t::encode_actor (
      actor_location_t{.actor_id = "alice",
                       .actor_type = "player",
                       .actor_ref = std::nullopt,
                       .node_rid = zlink::routing_id_t::from ("node-a"),
                       .location_kind = zlink::spot_kind::user,
                       .spot_mesh_name = "play",
                       .spot_rid = zlink::routing_id_t::from ("play-spot"),
                       .owner_id = "owner-a",
                       .generation = 11});

    const auto json = nlohmann::json::parse (encoded);
    EXPECT_EQ ("player", json.at ("ActorType").get<std::string> ());
    EXPECT_EQ ("alice", json.at ("ActorId").get<std::string> ());
    EXPECT_TRUE (json.at ("ActorRef").is_null ());
    EXPECT_EQ (zlink::routing_id_t::from ("node-a").to_hex (),
               json.at ("NodeRid").get<std::string> ());
    EXPECT_EQ (11, json.at ("Generation").get<int> ());
    EXPECT_EQ (static_cast<int> (zlink::spot_kind::user), json.at ("LocationKind").get<int> ());
    EXPECT_EQ (zlink::routing_id_t::from ("play-spot").to_hex (),
               json.at ("SpotRid").get<std::string> ());
    EXPECT_EQ ("play", json.at ("SpotMeshName").get<std::string> ());
    EXPECT_EQ ("owner-a", json.at ("OwnerId").get<std::string> ());

    const auto decoded = redis_location_row_codec_t::decode_actor (encoded);
    ASSERT_TRUE (decoded.actor_type.has_value ());
    EXPECT_EQ ("player", *decoded.actor_type);
    EXPECT_EQ ("alice", decoded.actor_id);
    EXPECT_EQ ("node-a", decoded.node_rid.to_string ());
    ASSERT_TRUE (decoded.spot_rid.has_value ());
    EXPECT_EQ ("play-spot", decoded.spot_rid->to_string ());
    EXPECT_EQ (11, decoded.generation);
}

TEST (ZLinkFrameworkLocationsRedis, EntryActorRowKeepsNullSpotRid)
{
    const auto encoded = redis_location_row_codec_t::encode_actor (
      actor_location_t{.actor_id = "bob",
                       .actor_type = "player",
                       .actor_ref = std::nullopt,
                       .node_rid = zlink::routing_id_t::from ("node-a"),
                       .location_kind = zlink::spot_kind::entry,
                       .spot_mesh_name = "play",
                       .spot_rid = std::nullopt,
                       .owner_id = "owner-a",
                       .generation = 3});

    const auto json = nlohmann::json::parse (encoded);
    EXPECT_TRUE (json.at ("SpotRid").is_null ());

    const auto decoded = redis_location_row_codec_t::decode_actor (encoded);
    EXPECT_FALSE (decoded.spot_rid.has_value ());
    EXPECT_EQ (zlink::spot_kind::entry, decoded.location_kind);
}

TEST (ZLinkFrameworkLocationsRedis, RouteRowJsonUsesBase64Value)
{
    const auto encoded = redis_location_row_codec_t::encode_route (
      route_location_t{.route_kind = route_kind_t::spot_name,
                       .route_key = "spot-a",
                       .owner_node_rid = zlink::routing_id_t::from ("node-a"),
                       .owner_id = "owner-a",
                       .generation = 5,
                       .value = {1, 2, 3, 4}});

    const auto json = nlohmann::json::parse (encoded);
    EXPECT_EQ (static_cast<int> (route_kind_t::spot_name), json.at ("RouteKind").get<int> ());
    EXPECT_EQ ("spot-a", json.at ("RouteKey").get<std::string> ());
    EXPECT_EQ (zlink::routing_id_t::from ("node-a").to_hex (),
               json.at ("OwnerNodeRid").get<std::string> ());
    EXPECT_EQ ("AQIDBA==", json.at ("Value").get<std::string> ());

    const auto decoded = redis_location_row_codec_t::decode_route (encoded);
    EXPECT_EQ (route_kind_t::spot_name, decoded.route_kind);
    EXPECT_EQ ("spot-a", decoded.route_key);
    EXPECT_EQ ("node-a", decoded.owner_node_rid.to_string ());
    EXPECT_EQ (std::vector<std::uint8_t> ({1, 2, 3, 4}), decoded.value);
}

} // namespace
