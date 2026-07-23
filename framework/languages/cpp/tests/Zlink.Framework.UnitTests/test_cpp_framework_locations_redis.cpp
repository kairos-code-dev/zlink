/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include <zlink/locations/redis.hpp>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <sw/redis++/redis++.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <thread>

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

std::string revision_hex (std::string_view value)
{
    std::ostringstream stream;
    stream << std::hex << std::nouppercase
           << std::setw (16) << std::setfill ('0')
           << std::stoull (std::string (value));
    return stream.str ();
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

nlohmann::json read_mesh_node_descriptor_fixture ()
{
    std::vector<std::filesystem::path> candidates;
    auto current = std::filesystem::current_path ();
    for (int i = 0; i < 8; ++i) {
        candidates.push_back (
          current
          / "framework/testdata/location/redis/mesh-node-descriptor-v1.json");
        candidates.push_back (
          current
          / "testdata/location/redis/mesh-node-descriptor-v1.json");
        candidates.push_back (
          current
          / "../../testdata/location/redis/mesh-node-descriptor-v1.json");
        current = current.parent_path ();
    }
    for (const auto &candidate : candidates) {
        std::ifstream input (candidate);
        if (input)
            return nlohmann::json::parse (input);
    }
    throw std::runtime_error (
      "mesh-node-descriptor-v1.json fixture was not found");
}

nlohmann::json read_authority_store_fixture ()
{
    std::vector<std::filesystem::path> candidates;
    auto current = std::filesystem::current_path ();
    for (int i = 0; i < 8; ++i) {
        candidates.push_back (
          current
          / "framework/testdata/location/redis/authority-store-v1.json");
        candidates.push_back (
          current
          / "testdata/location/redis/authority-store-v1.json");
        candidates.push_back (
          current
          / "../../testdata/location/redis/authority-store-v1.json");
        current = current.parent_path ();
    }
    for (const auto &candidate : candidates) {
        std::ifstream input (candidate);
        if (input)
            return nlohmann::json::parse (input);
    }
    throw std::runtime_error (
      "authority-store-v1.json fixture was not found");
}

const nlohmann::json &fixture_row (const nlohmann::json &fixture, std::string_view kind)
{
    if (fixture.contains ("row")) {
        const auto &row = fixture.at ("row");
        if (row.at ("kind").get<std::string> () == kind) {
            return row;
        }
    }
    for (const auto &row : fixture.at ("rows")) {
        if (row.at ("kind").get<std::string> () == kind) {
            return row;
        }
    }
    throw std::runtime_error ("fixture row was not found");
}

actor_location_t make_actor_location (std::string mesh_name,
                                      std::string actor_id,
                                      std::string node_rid,
                                      std::string spot_rid,
                                      zlink::spot_kind spot_kind,
                                      std::string owner_id,
                                      std::uint64_t actor_generation = 1,
                                      std::uint64_t owner_node_generation = 1,
                                      std::uint64_t spot_generation = 1,
                                      std::uint64_t membership_epoch = 1)
{
    const auto actor_type = std::string{"player"};
    return actor_location_t{
      .mesh_name = std::move (mesh_name),
      .actor_id = actor_id,
      .actor_type = actor_type,
      .actor_ref = actor_ref_t (node_rid_t::from_string (node_rid), actor_type,
                                actor_id, actor_generation),
      .owner_node_rid = zlink::routing_id_t::from (node_rid),
      .owner_node_generation = owner_node_generation,
      .spot_rid = zlink::routing_id_t::from (std::move (spot_rid)),
      .spot_generation = spot_generation,
      .spot_kind = spot_kind,
      .membership_epoch = membership_epoch,
      .owner_id = std::move (owner_id)};
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

TEST (ZLinkFrameworkLocationsRedis,
      PhysicalDomainRejectsCallerHashTags)
{
    EXPECT_THROW (
      (redis_location_store_t{
        redis_location_options_t{
          .connection_string =
            "tcp://127.0.0.1:6379",
          .key_prefix = "zlink:{caller-tag}"}}),
      std::invalid_argument);
}

TEST (ZLinkFrameworkLocationsRedis,
      MeshNodeDescriptorCodecMatchesCanonicalFixture)
{
    const auto fixture = read_mesh_node_descriptor_fixture ();
    const auto &row = fixture.at ("row");
    const auto &hash = row.at ("hash");
    const auto updated_at =
      std::chrono::system_clock::time_point{
        std::chrono::milliseconds{1721001600000}};
    zlink::framework::mesh_node_descriptor_t descriptor{
      .mesh_name = "game",
      .rid = zlink::routing_id_t::from ("game-a"),
      .lifecycle_generation = 7,
      .descriptor_revision = 3,
      .endpoint = "tcp://10.0.0.1:7300",
      .channel_weights = {{"orders", 100}, {"world", 50}},
      .application_version = 0,
      .object_role = zlink::framework::object_role_t::none,
      .placement_weight = 100,
      .object_capacity =
        {.active = 0,
         .pending = 0,
         .active_limit = 10000,
         .pending_limit = 128},
      .state = zlink::framework::framework_runtime_state_t::serving,
      .security_identity = "cluster-a",
      .owner_id = "mesh-owner-a",
      .lease_generation = 9,
      .updated_at = updated_at};

    EXPECT_EQ (
      hash.at ("json").get<std::string> (),
      redis_location_row_codec_t::encode_mesh_node (descriptor));
    EXPECT_EQ (
      fixture.at ("immutableDigest")
        .at ("preimage")
        .get<std::string> (),
      redis_location_row_codec_t::mesh_node_immutable_preimage (
        descriptor));
    EXPECT_EQ (
      fixture.at ("immutableDigest")
        .at ("sha256LowerHex")
        .get<std::string> (),
      redis_location_row_codec_t::mesh_node_immutable_digest (
        descriptor));
    const auto decoded =
      redis_location_row_codec_t::decode_mesh_node (
        hash.at ("json").get<std::string> ());
    EXPECT_EQ (descriptor.mesh_name, decoded.mesh_name);
    EXPECT_EQ (descriptor.rid, decoded.rid);
    EXPECT_EQ (descriptor.object_capacity.active,
               decoded.object_capacity.active);
    EXPECT_EQ (descriptor.object_capacity.pending,
               decoded.object_capacity.pending);
}

TEST (ZLinkFrameworkLocationsRedis, StoreWithoutRedisClientReportsUnavailable)
{
    redis_location_store_t store (redis_location_options_t{
      .connection_string = "tcp://127.0.0.1:1", .key_prefix = "zlink:test"});

    auto claim = store.update_actor (
      make_actor_location ("play", "alice", "node-a", "spot-a",
                           zlink::spot_kind::user, "owner-a"),
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

    auto read = store.resolve_actor (
      actor_location_key_t{.mesh_name = "play", .actor_id = "alice"});
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
      actor_location_key_t{.mesh_name = "play", .actor_id = "alice"},
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

    // The location store owns a persistent async connection. An idle interval
    // between the host lease and the first row claim must not close it.
    std::this_thread::sleep_for (std::chrono::milliseconds (600));

    const auto before =
      store.get_change_stamp ({.kind = location_kind_t::actor, .mesh_name = std::nullopt})
        .result ()
        .value ();
    auto claim = store.update_actor (
      make_actor_location ("play", "alice", "node-a", "spot-a",
                           zlink::spot_kind::user, "owner-a"),
      zlink::framework::location_write_intent_t::new_claim);
    ASSERT_EQ (location_write_status_t::stored, claim.result ().value ().status);
    ASSERT_GT (claim.result ().value ().generation, 0);

    sw::redis::Redis redis (options->connection_string);
    const auto actor_hash_key = redis_location_key_schema_t::row_key (
      options->key_prefix, location_kind_t::actor,
      redis_location_key_schema_t::encode_actor_key (
        actor_location_key_t{.mesh_name = "play", .actor_id = "alice"}));
    std::vector<std::string> actor_hash_fields;
    redis.hkeys (actor_hash_key, std::back_inserter (actor_hash_fields));
    EXPECT_EQ (5u, actor_hash_fields.size ());
    EXPECT_NE (actor_hash_fields.end (),
               std::find (actor_hash_fields.begin (), actor_hash_fields.end (), "mesh"));

    const auto after =
      store.get_change_stamp ({.kind = location_kind_t::actor, .mesh_name = std::nullopt})
        .result ()
        .value ();
    EXPECT_GT (after, before);

    auto resolved = store.resolve_actor (
      actor_location_key_t{.mesh_name = "play", .actor_id = "alice"});
    ASSERT_TRUE (resolved.result ().has_value ());
    ASSERT_TRUE (resolved.result ().value ().has_value ());
    EXPECT_EQ ("alice", resolved.result ().value ()->actor_id);
    EXPECT_EQ ("play", resolved.result ().value ()->mesh_name);
    EXPECT_EQ ("node-a", resolved.result ().value ()->owner_node_rid.to_string ());

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
    auto missing = store.resolve_actor (
      actor_location_key_t{.mesh_name = "play", .actor_id = "alice"});
    ASSERT_TRUE (missing.result ().has_value ());
    EXPECT_FALSE (missing.result ().value ().has_value ());
    (void) store.remove_all_by_owner ("publisher-owner");
    (void) store.remove_owner_lease ("publisher-owner");
    (void) store.remove_owner_lease ("owner-a");
#endif
}

TEST (ZLinkFrameworkLocationsRedis,
      MeshNodeDescriptorUsesExactLeaseAndImmutableRevisionFence)
{
#if !defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
    GTEST_SKIP () << "redis-plus-plus is not available in this build";
#else
    using namespace zlink::framework;
    const auto options = find_redis_options ();
    if (!options)
        GTEST_SKIP () << "Redis server is not available";
    redis_location_store_t store (*options);
    const auto claimed =
      store
        .claim_owner_lease (
          "descriptor-owner", std::chrono::seconds (10))
        .result ()
        .value ();
    const auto token =
      std::get<owner_lease_claimed_t> (claimed).token;
    mesh_node_descriptor_t descriptor{
      .mesh_name = "play",
      .rid = zlink::routing_id_t::from ("node-descriptor"),
      .lifecycle_generation = 7,
      .descriptor_revision = 1,
      .endpoint = "tcp://127.0.0.1:5001",
      .application_version = 11,
      .object_capabilities =
        {{.object_kind = placement_object_kind_t::actor,
          .stable_type = "player",
          .policy = maintenance_policy_kind_t::recreate,
          .placement_profiles = {"standard"},
          .pending_limit = 3}},
      .object_role = object_role_t::server,
      .object_capacity =
        {.active_limit = 100, .pending_limit = 5},
      .state = framework_runtime_state_t::serving,
      .security_identity = "test",
      .owner_id = token.owner_id,
      .lease_generation = token.lease_generation};
    EXPECT_EQ (
      location_write_status_t::stored,
      store
        .update_mesh_node (
          descriptor, location_write_intent_t::new_claim)
        .result ()
        .value ()
        .status);
    sw::redis::Redis redis (options->connection_string);
    std::vector<std::string> lease_fields;
    redis.hkeys (
      redis_location_key_schema_t::lease_key (
        options->key_prefix, token.owner_id),
      std::back_inserter (lease_fields));
    std::sort (
      lease_fields.begin (), lease_fields.end ());
    EXPECT_EQ (
      (std::vector<std::string>{
        "expiresAt", "generation", "ownerId"}),
      lease_fields);
    std::vector<std::string> descriptor_fields;
    redis.hkeys (
      redis_location_key_schema_t::mesh_node_key (
        options->key_prefix, "play", "node-descriptor"),
      std::back_inserter (descriptor_fields));
    std::sort (
      descriptor_fields.begin (), descriptor_fields.end ());
    EXPECT_EQ (
      (std::vector<std::string>{
        "gen", "json", "mesh", "owner", "updatedAtMs"}),
      descriptor_fields);
    const auto admission_key =
      redis_location_key_schema_t::
        mesh_node_admission_key (
          options->key_prefix, "play",
          "node-descriptor");
    std::vector<std::string> admission_fields;
    redis.hkeys (
      admission_key,
      std::back_inserter (admission_fields));
    std::sort (
      admission_fields.begin (),
      admission_fields.end ());
    auto expected_admission_fields =
      read_mesh_node_descriptor_fixture ()
        .at ("admissionHashFields")
        .get<std::vector<std::string>> ();
    std::sort (
      expected_admission_fields.begin (),
      expected_admission_fields.end ());
    EXPECT_EQ (
      expected_admission_fields,
      admission_fields);
    std::vector<std::pair<std::string, std::string>>
      admission_entries;
    redis.hgetall (
      admission_key,
      std::back_inserter (admission_entries));
    const std::map<std::string, std::string> admission (
      admission_entries.begin (), admission_entries.end ());
    EXPECT_EQ (
      redis_location_key_schema_t::
        encode_mesh_node_key (
          {"play",
           zlink::routing_id_t::from (
             "node-descriptor")}),
      admission.at ("descriptorKey"));
    EXPECT_EQ ("1", admission.at ("descriptorRevision"));
    EXPECT_EQ ("7", admission.at ("lifecycleGeneration"));
    EXPECT_EQ (token.owner_id, admission.at ("ownerId"));
    EXPECT_EQ (
      std::to_string (token.lease_generation),
      admission.at ("ownerLeaseGeneration"));
    EXPECT_EQ ("2", admission.at ("objectRole"));
    EXPECT_EQ ("1", admission.at ("runtimeState"));
    EXPECT_EQ ("11", admission.at ("applicationVersion"));
    EXPECT_EQ ("100", admission.at ("nodeActiveLimit"));
    EXPECT_EQ ("5", admission.at ("nodePendingLimit"));
    EXPECT_EQ (64u, admission.at ("immutableDigest").size ());
    const auto capabilities = nlohmann::json::parse (
      admission.at ("capabilities"));
    ASSERT_EQ (1u, capabilities.size ());
    EXPECT_EQ (
      "actor",
      capabilities.at (0).at ("objectKind"));
    EXPECT_EQ (
      "player",
      capabilities.at (0).at ("stableType"));
    EXPECT_TRUE (
      redis.sismember (
        redis_location_key_schema_t::
          mesh_node_owner_keys_key (
            options->key_prefix, token.owner_id,
            token.lease_generation),
        redis_location_key_schema_t::
          encode_mesh_node_key (
            {"play",
             zlink::routing_id_t::from (
               "node-descriptor")})));
    const auto listed =
      store.list_mesh_nodes ("play").result ().value ();
    ASSERT_EQ (1u, listed.items.size ());
    EXPECT_EQ (7u, listed.items.front ().lifecycle_generation);
    EXPECT_EQ (
      std::optional<std::uint32_t>{3},
      listed.items.front ()
        .object_capabilities.front ()
        .pending_limit);

    descriptor.descriptor_revision = 2;
    descriptor.object_capacity.pending_limit = 6;
    EXPECT_EQ (
      location_write_status_t::rejected_conflict,
      store
        .update_mesh_node (
          descriptor, location_write_intent_t::renew)
        .result ()
        .value ()
        .status);

    object_reserve_request_t wrong_profile{
      .key = {placement_object_kind_t::actor,
              "redis-profile-wrong"},
      .intent =
        {.stable_type = "player",
         .placement_profile =
           placement_profile_t{"premium"}},
      .target =
        {.mesh_name = "play",
         .node_rid =
           node_rid_t::from_string ("node-descriptor"),
         .node_lifecycle_generation = 7,
         .owner = token}};
    EXPECT_NE (
      nullptr,
      std::get_if<object_reserve_conflict_t> (
        &store.reserve (wrong_profile).result ().value ()));

    auto creation = wrong_profile;
    creation.key.global_id = "redis-profiled";
    creation.intent.placement_profile =
      placement_profile_t{"standard"};
    const auto reserved =
      store.reserve (creation).result ().value ();
    const auto *reserved_value =
      std::get_if<object_reserved_t> (&reserved);
    ASSERT_NE (nullptr, reserved_value);
    const auto committed =
      store
        .commit (
          {creation.key, reserved_value->fence,
           {std::byte{0x01}}})
        .result ()
        .value ();
    const auto *committed_value =
      std::get_if<object_committed_t> (&committed);
    ASSERT_NE (nullptr, committed_value);
    std::vector<std::string> authority_fields;
    redis.hkeys (
      redis_location_key_schema_t::authority_key (
        options->key_prefix, "1:redis-profiled"),
      std::back_inserter (authority_fields));
    std::sort (
      authority_fields.begin (), authority_fields.end ());
    auto fixture_fields =
      read_authority_store_fixture ()
        .at ("currentHashFields")
        .get<std::vector<std::string>> ();
    std::sort (
      fixture_fields.begin (), fixture_fields.end ());
    EXPECT_EQ (fixture_fields, authority_fields);
    const auto authority_scan =
      store.list_authorities ("1:", std::nullopt, 1)
        .result ()
        .value ();
    const auto *authority_page =
      std::get_if<authority_page_t> (&authority_scan);
    ASSERT_NE (nullptr, authority_page);
    ASSERT_EQ (1u, authority_page->items.size ());
    EXPECT_EQ (
      "1:redis-profiled",
      authority_page->items.front ().key.value);

    const auto target_token =
      std::get<owner_lease_claimed_t> (
        store
          .claim_owner_lease (
            "descriptor-target-owner",
            std::chrono::seconds (10))
          .result ()
          .value ())
        .token;
    auto target_descriptor = descriptor;
    target_descriptor.rid =
      zlink::routing_id_t::from ("node-target");
    target_descriptor.owner_id = target_token.owner_id;
    target_descriptor.lease_generation =
      target_token.lease_generation;
    target_descriptor.object_capacity.pending_limit = 1;
    target_descriptor.object_capabilities.front ().pending_limit =
      1;
    EXPECT_EQ (
      location_write_status_t::stored,
      store
        .update_mesh_node (
          target_descriptor,
          location_write_intent_t::new_claim)
        .result ()
        .value ()
        .status);

    std::array<std::byte, 16> relocation_id{};
    relocation_id[15] = std::byte{0x45};
    relocation_capacity_reserve_request_t relocation{
      .reservation_id = relocation_id,
      .key = {"1:redis-profiled"},
      .expected_store_version =
        committed_value->ready.store_version,
      .object_kind = placement_object_kind_t::actor,
      .stable_type = "player",
      .source = creation.target,
      .target =
        {.mesh_name = "play",
         .node_rid = node_rid_t::from_string ("node-target"),
         .node_lifecycle_generation = 7,
         .owner = target_token}};
    const auto capacity =
      store.reserve_relocation_capacity (relocation)
        .result ()
        .value ();
    const auto *capacity_value =
      std::get_if<relocation_capacity_reserved_t> (&capacity);
    ASSERT_NE (nullptr, capacity_value);

    target_descriptor.descriptor_revision = 3;
    target_descriptor.state =
      framework_runtime_state_t::retiring;
    EXPECT_EQ (
      location_write_status_t::stored,
      store
        .update_mesh_node (
          target_descriptor,
          location_write_intent_t::renew)
        .result ()
        .value ()
        .status);
    const auto stale_target =
      store
        .compare_exchange_authority (
          {"1:redis-profiled"},
          committed_value->ready.store_version,
          authority_put_t{
            {std::byte{0x02}},
            authority_generation_transition_t::new_owner,
            target_token,
            capacity_value->fence})
        .result ()
        .value ();
    EXPECT_NE (
      nullptr,
      std::get_if<authority_conflict_t> (&stale_target));

    target_descriptor.descriptor_revision = 4;
    target_descriptor.state =
      framework_runtime_state_t::serving;
    ASSERT_EQ (
      location_write_status_t::stored,
      store
        .update_mesh_node (
          target_descriptor,
          location_write_intent_t::renew)
        .result ()
        .value ()
        .status);
    aggregate_prepare_request_t aggregate;
    aggregate.aggregate_id.value[15] =
      std::byte{0x7a};
    aggregate.aggregate_generation = 1;
    aggregate.participants = {
      {{"1:redis-profiled"},
       committed_value->ready.store_version,
       authority_generation_transition_t::new_owner,
       {std::byte{0x03}},
       {}}};
    aggregate.target_owner = target_token;
    aggregate.target_reservations = {
      capacity_value->fence};
    const auto prepared =
      store.prepare_aggregate (aggregate)
        .result ()
        .value ();
    const auto *prepared_value =
      std::get_if<aggregate_prepared_t> (&prepared);
    ASSERT_NE (nullptr, prepared_value);
    EXPECT_EQ (
      aggregate_commit_result_t::committed,
      store.commit_aggregate (
        prepared_value->fence)
        .result ()
        .value ());
    const auto moved =
      std::get<authority_snapshot_t> (
        store.read_authority (
          {"1:redis-profiled"})
          .result ()
          .value ());
    EXPECT_EQ (
      target_token.owner_id,
      moved.owner.owner_id);
    EXPECT_EQ (
      std::vector<std::byte>{std::byte{0x03}},
      moved.payload);
    authority_fields.clear ();
    redis.hkeys (
      redis_location_key_schema_t::authority_key (
        options->key_prefix, "1:redis-profiled"),
      std::back_inserter (authority_fields));
    std::sort (
      authority_fields.begin (), authority_fields.end ());
    EXPECT_EQ (fixture_fields, authority_fields);
    const auto aggregate_revision =
      revision_hex (
        committed_value->ready.store_version);
    const auto aggregate_deleted = redis.hget (
        redis_location_key_schema_t::
          authority_history_key (
            options->key_prefix,
            "1:redis-profiled"),
        aggregate_revision + ":deleted");
    ASSERT_TRUE (aggregate_deleted);
    EXPECT_EQ ("0", *aggregate_deleted);
    const auto aggregate_payload = redis.hget (
        redis_location_key_schema_t::
          authority_history_key (
            options->key_prefix,
            "1:redis-profiled"),
        aggregate_revision + ":payload");
    ASSERT_TRUE (aggregate_payload);
    EXPECT_EQ (
      std::string (1, '\x01'),
      *aggregate_payload);

    const auto create_for_scan =
      [&] (std::string id, std::byte marker) {
        object_reserve_request_t request{
          .key = {placement_object_kind_t::actor,
                  std::move (id)},
          .intent = {.stable_type = "player"},
          .target = creation.target,
          .creating_payload = {marker}};
        const auto reserve_result =
          store.reserve (request).result ().value ();
        const auto *reserve_value =
          std::get_if<object_reserved_t> (
            &reserve_result);
        EXPECT_NE (nullptr, reserve_value);
        if (reserve_value == nullptr)
            return authority_snapshot_t{};
        return std::get<object_committed_t> (
                 store
                   .commit (
                     {request.key, reserve_value->fence,
                      {marker}})
                   .result ()
                   .value ())
          .ready;
      };
    const auto scan_a =
      create_for_scan ("scan-a", std::byte{0x11});
    const auto scan_b =
      create_for_scan ("scan-b", std::byte{0x12});
    const auto scan_d =
      create_for_scan ("scan-d", std::byte{0x14});
    const auto first_scan =
      std::get<authority_page_t> (
        store.list_authorities (
          "1:scan-", std::nullopt, 1)
          .result ()
          .value ());
    ASSERT_EQ (1u, first_scan.items.size ());
    ASSERT_TRUE (first_scan.next_cursor.has_value ());
    EXPECT_EQ ("1:scan-a",
               first_scan.items.front ().key.value);

    ASSERT_NE (
      nullptr,
      std::get_if<authority_stored_t> (
        &store
           .compare_exchange_authority (
             {"1:scan-b"}, scan_b.store_version,
             authority_put_t{
               {std::byte{0x22}},
               authority_generation_transition_t::preserve,
               std::nullopt})
           .result ()
           .value ()));
    const auto delete_result =
      store
        .compare_exchange_authority (
          {"1:scan-d"}, scan_d.store_version,
          authority_delete_t{})
        .result ()
        .value ();
    const auto *deleted =
      std::get_if<authority_deleted_t> (
        &delete_result);
    ASSERT_NE (nullptr, deleted);
    const auto deleted_revision =
      revision_hex (deleted->store_version);
    const auto tombstone_deleted = redis.hget (
        redis_location_key_schema_t::
          authority_history_key (
            options->key_prefix, "1:scan-d"),
        deleted_revision + ":deleted");
    ASSERT_TRUE (tombstone_deleted);
    EXPECT_EQ ("1", *tombstone_deleted);
    const auto tombstone_key = redis.hget (
        redis_location_key_schema_t::
          authority_history_key (
            options->key_prefix, "1:scan-d"),
        deleted_revision + ":authorityKey");
    ASSERT_TRUE (tombstone_key);
    EXPECT_EQ ("1:scan-d", *tombstone_key);
    (void) create_for_scan (
      "scan-c", std::byte{0x13});

    std::vector<authority_entry_t> remaining;
    auto cursor = first_scan.next_cursor;
    while (cursor) {
        const auto page =
          std::get<authority_page_t> (
            store.list_authorities (
              "1:scan-", cursor, 1)
              .result ()
              .value ());
        remaining.insert (
          remaining.end (), page.items.begin (),
          page.items.end ());
        cursor = page.next_cursor;
    }
    ASSERT_EQ (2u, remaining.size ());
    EXPECT_EQ ("1:scan-b", remaining[0].key.value);
    EXPECT_EQ (
      std::vector<std::byte>{std::byte{0x12}},
      remaining[0].snapshot.payload);
    EXPECT_EQ ("1:scan-d", remaining[1].key.value);
    EXPECT_EQ (
      std::vector<std::byte>{std::byte{0x14}},
      remaining[1].snapshot.payload);
#endif
}

TEST (ZLinkFrameworkLocationsRedis, PagedActorListUsesOpaqueRedisScanCursor)
{
#if !defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
    GTEST_SKIP () << "redis-plus-plus is not available in this build";
#else
    const auto options = find_redis_options ();
    if (!options) {
        GTEST_SKIP () << "Redis is not reachable; set ZLINK_REDIS_TEST_ENDPOINT to enable";
    }

    redis_location_store_t store (*options);
    ASSERT_TRUE (store
                   .renew_owner_lease ("scan-owner", zlink::routing_id_t::from ("scan-node"),
                                       std::chrono::seconds (10))
                   .result ()
                   .has_value ());

    std::vector<std::string> expected;
    for (int index = 0; index < 25; ++index) {
        const auto actor_id = "scan-actor-" + std::to_string (index);
        const auto is_player = index % 2 == 0;
        const auto actor_type = is_player ? "player" : "npc";
        auto actor_row = make_actor_location (
          "play", actor_id, "scan-node", "scan-spot",
          zlink::spot_kind::user, "scan-owner");
        actor_row.actor_type = actor_type;
        actor_row.actor_ref = actor_ref_t (
          node_rid_t::from_string ("scan-node"), actor_type, actor_id, 1);
        auto claim = store.update_actor (
          std::move (actor_row),
          zlink::framework::location_write_intent_t::new_claim);
        ASSERT_EQ (location_write_status_t::stored, claim.result ().value ().status);
        if (is_player) {
            expected.push_back (actor_id);
        }
    }

    std::vector<std::string> actual;
    std::optional<std::string> continuation;
    int page_count = 0;
    do {
        auto page = store.list_actors (
          zlink::framework::actor_location_filter_t{.actor_type = "player"},
          zlink::framework::location_page_request_t{.page_size = 3,
                                                    .continuation_token = continuation});
        ASSERT_TRUE (page.result ().has_value ());
        EXPECT_LE (page.result ().value ().items.size (), 3u);
        for (const auto &actor : page.result ().value ().items) {
            actual.push_back (actor.actor_id);
        }
        continuation = page.result ().value ().continuation_token;
        if (continuation) {
            const auto token = nlohmann::json::parse (*continuation);
            EXPECT_TRUE (token.at ("cursor").is_string ());
            EXPECT_TRUE (token.at ("pending").is_array ());
        }
        ++page_count;
        ASSERT_LT (page_count, 50);
    } while (continuation);

    std::sort (expected.begin (), expected.end ());
    std::sort (actual.begin (), actual.end ());
    EXPECT_EQ (expected, actual);
    EXPECT_GT (page_count, 1);
    (void) store.remove_all_by_owner ("scan-owner");
    (void) store.remove_owner_lease ("scan-owner");
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
                      make_actor_location ("mesh", "cpp-actor", "cpp-node", "cpp-spot",
                                           zlink::spot_kind::user, "cpp-owner"),
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
    auto actor = store.resolve_actor (
                   actor_location_key_t{.mesh_name = "mesh", .actor_id = "dotnet-actor"})
                   .result ();
    ASSERT_TRUE (actor.has_value ());
    ASSERT_TRUE (actor.value ().has_value ());
    EXPECT_FALSE (actor.value ()->actor_ref.empty ());
    EXPECT_EQ ("dotnet-actor", actor.value ()->actor_ref.actor_id ());
    EXPECT_EQ ("dotnet-node", actor.value ()->actor_ref.node_rid ().value ());
    EXPECT_EQ (1u, actor.value ()->actor_ref.generation ());
    EXPECT_EQ ("dotnet-node", actor.value ()->owner_node_rid.to_string ());
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
    EXPECT_NE (std::string::npos, write.find ("redis.call('EXISTS', KEYS[4])"));
    EXPECT_NE (std::string::npos, write.find ("return {'conflict', 0, nowMs}"));
    EXPECT_NE (std::string::npos, write.find ("local gen = redis.call('INCR', KEYS[2])"));
    EXPECT_NE (std::string::npos, write.find ("'owner', owner, 'gen', gen, 'json', ARGV[4]"));
    EXPECT_NE (std::string::npos, write.find ("redis.call('SADD', KEYS[5], ARGV[5])"));
    EXPECT_NE (std::string::npos, write.find ("tonumber(redis.call('HGET', KEYS[1], 'gen'))"));
    EXPECT_NE (std::string::npos, write.find ("return {'stale', 0, nowMs}"));

    const auto remove = std::string (redis_location_scripts_t::remove);
    EXPECT_NE (std::string::npos, remove.find ("redis.call('DEL', KEYS[1])"));
    EXPECT_NE (std::string::npos, remove.find ("redis.call('SREM', KEYS[2], ARGV[3])"));
    EXPECT_NE (std::string::npos, remove.find ("redis.call('SREM', KEYS[3], ARGV[3])"));
    EXPECT_EQ (std::string::npos, remove.find ("DEL', KEYS[2]"));

    const auto renew_lease = std::string (redis_location_scripts_t::renew_lease);
    EXPECT_NE (
      std::string::npos,
      renew_lease.find ("redis.call('PEXPIRE', KEYS[1], ARGV[3])"));
    EXPECT_NE (
      std::string::npos,
      renew_lease.find ("'ownerId', ARGV[1]"));
    EXPECT_NE (
      std::string::npos,
      renew_lease.find ("'expiresAt', expiresAtMs"));
    EXPECT_NE (
      std::string::npos,
      renew_lease.find (
        "redis.call('HSET', KEYS[2], ARGV[1], ARGV[2] .. '|' .. nowMs)"));

    const auto reserve_relocation =
      std::string (
        redis_location_scripts_t::reserve_relocation_capacity);
    EXPECT_NE (
      std::string::npos,
      reserve_relocation.find (
        "if not liveLease(KEYS[3], ARGV[12])"));
    EXPECT_NE (
      std::string::npos,
      reserve_relocation.find (
        "redis.call('HGET', KEYS[9], 'lifecycleGeneration')"));
    EXPECT_NE (
      std::string::npos,
      reserve_relocation.find (
        "redis.call('HGET', KEYS[9], 'nodePendingLimit')"));
    EXPECT_EQ (
      std::string::npos,
      reserve_relocation.find (
        "liveLease(KEYS[2], ARGV[9])"));

    const auto prepare_aggregate =
      std::string (
        redis_location_scripts_t::prepare_aggregate);
    EXPECT_NE (
      std::string::npos,
      prepare_aggregate.find (
        "'status', 'prepared',"));
    EXPECT_NE (
      std::string::npos,
      prepare_aggregate.find (
        "'aggregateGeneration', ARGV[3]"));
    EXPECT_EQ (
      std::string::npos,
      prepare_aggregate.find (
        "prefix .. 'targetDescriptorRedisKey'"));
    EXPECT_NE (
      std::string::npos,
      prepare_aggregate.find (
        "local keyBase = 5 + (i - 1) * 14"));

    const auto commit_aggregate =
      std::string (
        redis_location_scripts_t::commit_aggregate);
    EXPECT_NE (
      std::string::npos,
      commit_aggregate.find (
        "if not liveLease(KEYS[4], targetGeneration) then"));
    EXPECT_NE (
      std::string::npos,
      commit_aggregate.find (
        "return 'stale'"));

    const auto abort_relocation =
      std::string (
        redis_location_scripts_t::abort_relocation_capacity);
    EXPECT_NE (
      std::string::npos,
      abort_relocation.find (
        "if status ~= 'reserved' then return 'stale' end"));
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
    EXPECT_EQ ("zlink:test:{zlink-location-v1}:row:peer:" + peer_key,
               redis_location_key_schema_t::row_key ("zlink:test", location_kind_t::peer,
                                                     peer_key));
    EXPECT_EQ ("zlink:test:{zlink-location-v1}:gen:peer:" + peer_key,
               redis_location_key_schema_t::generation_key ("zlink:test", location_kind_t::peer,
                                                            peer_key));
    EXPECT_EQ ("zlink:test:{zlink-location-v1}:keys:peer",
               redis_location_key_schema_t::keys_key ("zlink:test", location_kind_t::peer));
    EXPECT_EQ ("zlink:test:{zlink-location-v1}:own:peer:owner-a",
               redis_location_key_schema_t::owner_key ("zlink:test", location_kind_t::peer,
                                                       "owner-a"));
    EXPECT_EQ (
      "zlink:test:{zlink-location-v1}:owner-lease:"
      "95256875151043abdcafdd26fd390c650d6311e1d7185df477ce50736b6a5d0b",
               redis_location_key_schema_t::lease_key ("zlink:test", "owner-a"));
    EXPECT_EQ ("zlink:test:{zlink-location-v1}:leases",
               redis_location_key_schema_t::leases_key ("zlink:test"));
    EXPECT_EQ ("zlink:test:{zlink-location-v1}:stamp:spot:mesh-a",
               redis_location_key_schema_t::stamp_key ("zlink:test", location_kind_t::spot,
                                                       std::string_view ("mesh-a")));
    const auto mesh_key =
      redis_location_key_schema_t::encode_mesh_node_key (
        {.mesh_name = "game",
         .rid = zlink::routing_id_t::from ("game-a")});
    EXPECT_EQ ("4:game12:67616d652d61", mesh_key);
    EXPECT_EQ (
      "zlink:test:{zlink-location-v1}:descriptor:mesh:"
      "d865b668dc208572007a1d65fecd4111be06f76455502ba2939a3661e96c72fa",
      redis_location_key_schema_t::mesh_node_key (
        "zlink:test", "game", "game-a"));
    EXPECT_EQ (
      "zlink:test:{zlink-location-v1}:descriptor:mesh:index",
      redis_location_key_schema_t::mesh_node_keys_key (
        "zlink:test", "game"));
}

TEST (ZLinkFrameworkLocationsRedis, RowKeysUseDotnetCanonicalKeyBytes)
{
    EXPECT_EQ ("9:mesh-main12:73706f742d61",
               redis_location_key_schema_t::encode_spot_key (
                 spot_location_key_t{.mesh_name = "mesh-main",
                                     .spot_rid = zlink::routing_id_t::from ("spot-a")}));
    EXPECT_EQ ("4:game7:actor-1",
               redis_location_key_schema_t::encode_actor_key (
                 actor_location_key_t{.mesh_name = "game", .actor_id = "actor-1"}));
    EXPECT_EQ ("1:113:session:alpha",
               redis_location_key_schema_t::encode_route_key (
                 route_location_key_t{.route_kind = route_kind_t::actor_session,
                                      .route_key = "session:alpha"}));
}

TEST (ZLinkFrameworkLocationsRedis,
      AuthorityFixtureMatchesCommonRedisContract)
{
    const auto fixture = read_redis_location_fixture ();
    EXPECT_EQ (
      "actor-location-v2",
      fixture.at ("format").get<std::string> ());
    const auto &actor = fixture_row (fixture, "actor");
    const auto key = actor.at ("key").get<std::string> ();
    EXPECT_EQ ("zla1:a:4:game:7:actor-1", key);
    EXPECT_EQ (
      "zlink:test:{zlink-location-v1}:authority:current:"
      "9ee2d523d30641eef7d7ee225877d4b10d3dd2528ca4701e44498b157eaf8a2e",
      redis_location_key_schema_t::authority_key (
        "zlink:test", key));
    const auto &hash = actor.at ("hash");
    EXPECT_EQ (
      "opaque-actor-authority-v1",
      hash.at ("payload").get<std::string> ());
    EXPECT_EQ ("101", hash.at ("storeVersion").get<std::string> ());
    EXPECT_EQ (
      "11", hash.at ("objectGeneration").get<std::string> ());
    EXPECT_EQ (
      "4",
      hash.at ("authorityOwnerGeneration").get<std::string> ());
    EXPECT_EQ (
      "actor-owner-a", hash.at ("ownerId").get<std::string> ());
    EXPECT_EQ (
      "9",
      hash.at ("ownerLeaseGeneration").get<std::string> ());
}

TEST (ZLinkFrameworkLocationsRedis,
      AuthorityStoreFixtureFixesHybridKeysFieldsAndCapacityBuckets)
{
    const auto fixture = read_authority_store_fixture ();
    EXPECT_EQ (
      "location-authority-hybrid-v1",
      fixture.at ("format").get<std::string> ());
    EXPECT_EQ (
      "P:{zlink-location-v1}:authority:current:"
      "e1bef6b5eb5acdca14cc552bc25e8f7f33441cbb9bc3e0140ec1504fb2c40985",
      redis_location_key_schema_t::authority_key (
        "P", fixture.at ("keyContract").at ("authorityKey")
               .get<std::string> ()));

    const std::vector<std::string> expected_fields{
      "authorityKey", "payload", "storeVersion",
      "objectGeneration", "authorityOwnerGeneration",
      "ownerId", "ownerLeaseGeneration", "allocationState",
      "objectKind", "stableType", "descriptorKey",
      "descriptorLifecycleGeneration", "capacityDelta"};
    EXPECT_EQ (
      expected_fields,
      fixture.at ("currentHashFields")
        .get<std::vector<std::string>> ());
    auto expected_history_fields = expected_fields;
    expected_history_fields.insert (
      expected_history_fields.begin (), "deleted");
    EXPECT_EQ (
      expected_history_fields,
      fixture.at ("historyEncoding")
        .at ("fullSnapshotSuffixes")
        .get<std::vector<std::string>> ());
    EXPECT_EQ (
      (std::vector<std::string>{
        "deleted", "authorityKey"}),
      fixture.at ("historyEncoding")
        .at ("tombstoneSuffixes")
        .get<std::vector<std::string>> ());

    const auto &buckets = fixture.at ("capacityBuckets");
    const auto node = redis_location_key_schema_t::
      capacity_node_field (
        "game", "game-a", 7);
    const auto type = redis_location_key_schema_t::
      capacity_type_field (
        "game", "game-a", 7,
        zlink::framework::placement_object_kind_t::user_spot,
        "Game.Session");
    const auto unicode_type = redis_location_key_schema_t::
      capacity_type_field (
        "game", "game-a", 7,
        zlink::framework::placement_object_kind_t::user_spot,
        "룸.세션");
    EXPECT_EQ (
      buckets.at ("node").get<std::string> (), node);
    EXPECT_EQ (
      buckets.at ("type").get<std::string> (), type);
    EXPECT_EQ (
      buckets.at ("unicodeType").get<std::string> (),
      unicode_type);
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
      make_actor_location ("play", "alice", "node-a", "play-spot",
                           zlink::spot_kind::user, "owner-a", 11, 7, 3, 4));

    const auto json = nlohmann::json::parse (encoded);
    EXPECT_EQ ("play", json.at ("MeshName").get<std::string> ());
    EXPECT_EQ ("player", json.at ("ActorType").get<std::string> ());
    EXPECT_EQ ("alice", json.at ("ActorId").get<std::string> ());
    EXPECT_FALSE (json.at ("ActorRef").is_null ());
    EXPECT_EQ (zlink::routing_id_t::from ("node-a").to_hex (),
               json.at ("OwnerNodeRid").get<std::string> ());
    EXPECT_EQ (7u, json.at ("OwnerNodeGeneration").get<std::uint64_t> ());
    EXPECT_EQ (zlink::routing_id_t::from ("play-spot").to_hex (),
               json.at ("SpotRid").get<std::string> ());
    EXPECT_EQ (3u, json.at ("SpotGeneration").get<std::uint64_t> ());
    EXPECT_EQ (static_cast<int> (zlink::spot_kind::user), json.at ("SpotKind").get<int> ());
    EXPECT_EQ (4u, json.at ("MembershipEpoch").get<std::uint64_t> ());
    EXPECT_EQ ("owner-a", json.at ("OwnerId").get<std::string> ());

    const auto decoded = redis_location_row_codec_t::decode_actor (encoded);
    EXPECT_EQ ("player", decoded.actor_type);
    EXPECT_EQ ("alice", decoded.actor_id);
    EXPECT_EQ ("node-a", decoded.owner_node_rid.to_string ());
    EXPECT_EQ ("play-spot", decoded.spot_rid.to_string ());
    EXPECT_EQ (11u, decoded.actor_ref.generation ());
}

TEST (ZLinkFrameworkLocationsRedis, EntryActorRowKeepsTypedSpotIdentity)
{
    const auto encoded = redis_location_row_codec_t::encode_actor (
      make_actor_location ("play", "bob", "node-a", "entry-spot",
                           zlink::spot_kind::entry, "owner-a", 3));

    const auto json = nlohmann::json::parse (encoded);
    EXPECT_EQ (zlink::routing_id_t::from ("entry-spot").to_hex (),
               json.at ("SpotRid").get<std::string> ());

    const auto decoded = redis_location_row_codec_t::decode_actor (encoded);
    EXPECT_EQ ("entry-spot", decoded.spot_rid.to_string ());
    EXPECT_EQ (zlink::spot_kind::entry, decoded.spot_kind);
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
