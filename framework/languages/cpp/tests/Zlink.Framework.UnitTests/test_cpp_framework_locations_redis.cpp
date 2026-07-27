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

using zlink::framework::client_server_server_descriptor_key_t;
using zlink::framework::client_server_server_descriptor_t;
using zlink::framework::creation_operation_identity_t;
using zlink::framework::fanout_publisher_descriptor_key_t;
using zlink::framework::fanout_publisher_descriptor_t;
using zlink::framework::framework_runtime_state_t;
using zlink::framework::location_owner_token_t;
using zlink::framework::location_role_t;
using zlink::framework::node_rid_t;
using zlink::framework::location_write_status_t;
using zlink::framework::locations::redis::detail::redis_location_key_schema_t;
using zlink::framework::locations::redis::detail::redis_location_row_codec_t;
using zlink::framework::locations::redis::detail::redis_location_script_result_t;
using zlink::framework::locations::redis::detail::redis_location_scripts_t;
using zlink::framework::locations::redis::redis_location_options_t;
using zlink::framework::locations::redis::redis_location_store_t;
using zlink::framework::locations::redis::redis_relocation_options_t;
using zlink::framework::locations::redis::redis_relocation_store_t;

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

std::array<std::byte, 32> sha256_bytes (
  std::string_view value)
{
    const auto encoded =
      redis_location_key_schema_t::sha256_hex (value);
    const auto nibble = [] (char digit) {
        return digit >= '0' && digit <= '9'
                 ? static_cast<unsigned> (digit - '0')
                 : static_cast<unsigned> (digit - 'a' + 10);
    };
    std::array<std::byte, 32> result{};
    for (std::size_t index = 0; index < result.size ();
         ++index)
        result[index] = static_cast<std::byte> (
          (nibble (encoded[index * 2]) << 4u)
          | nibble (encoded[index * 2 + 1]));
    return result;
}

TEST (ZLinkFrameworkLocationsRedis,
      CreationTerminalKeyUsesRoutingIdRawBytes)
{
    const creation_operation_identity_t operation{
      node_rid_t::from_string ("node-a"), 7, {0x12, 0x34}};

    EXPECT_EQ (
      "P:{zlink-location-v3}:creation-terminal:"
      "6:6e6f64652d61:7:"
      "00000000000000120000000000000034",
      redis_location_key_schema_t::creation_terminal_key (
        "P", operation));
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

location_owner_token_t claim_owner (
  redis_location_store_t &store,
  std::string owner_id,
  std::chrono::milliseconds ttl = std::chrono::seconds (30))
{
    const auto claimed =
      store.claim_owner_lease (owner_id, ttl)
        .result ()
        .value ();
    const auto *value =
      std::get_if<zlink::framework::owner_lease_claimed_t> (
        &claimed);
    if (value == nullptr)
        throw std::runtime_error (
          "test owner lease claim failed");
    return value->token;
}

std::optional<redis_location_options_t> find_redis_options ()
{
#if defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
    for (const auto &endpoint : redis_test_endpoints ()) {
        redis_location_options_t options{.connection_string = endpoint,
                                         .key_prefix = unique_prefix ()};
        redis_location_store_t store (options);
        auto probe = store.claim_owner_lease (
          "probe-owner", std::chrono::milliseconds (250));
        if (probe.result ().has_value ()
            && std::holds_alternative<
                 zlink::framework::owner_lease_claimed_t> (
                 probe.result ().value ())) {
            const auto token =
              std::get<zlink::framework::owner_lease_claimed_t> (
                probe.result ().value ())
                .token;
            (void) store.release_owner_lease (token);
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

location_owner_token_t live_owner_token (
  redis_location_store_t &store,
  const std::string &owner_id)
{
    const auto lease =
      store.read_owner_lease (owner_id)
        .result ()
        .value ();
    const auto *found =
      std::get_if<
        zlink::framework::owner_lease_found_t> (
        &lease);
    if (found == nullptr)
        throw std::runtime_error (
          "test owner lease is not active");
    return found->token;
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

nlohmann::json read_client_server_descriptor_fixture ()
{
    std::vector<std::filesystem::path> candidates;
    auto current = std::filesystem::current_path ();
    for (int i = 0; i < 8; ++i) {
        candidates.push_back (
          current
          / "framework/testdata/location/redis/client-server-server-descriptor-v1.json");
        candidates.push_back (
          current
          / "testdata/location/redis/client-server-server-descriptor-v1.json");
        candidates.push_back (
          current
          / "../../testdata/location/redis/client-server-server-descriptor-v1.json");
        current = current.parent_path ();
    }
    for (const auto &candidate : candidates) {
        std::ifstream input (candidate);
        if (input)
            return nlohmann::json::parse (input);
    }
    throw std::runtime_error (
      "client-server-server-descriptor-v1.json fixture was not found");
}

nlohmann::json read_fanout_publisher_descriptor_fixture ()
{
    std::vector<std::filesystem::path> candidates;
    auto current = std::filesystem::current_path ();
    for (int i = 0; i < 8; ++i) {
        candidates.push_back (
          current
          / "framework/testdata/location/redis/fanout-publisher-descriptor-v1.json");
        candidates.push_back (
          current
          / "testdata/location/redis/fanout-publisher-descriptor-v1.json");
        candidates.push_back (
          current
          / "../../testdata/location/redis/fanout-publisher-descriptor-v1.json");
        current = current.parent_path ();
    }
    for (const auto &candidate : candidates) {
        std::ifstream input (candidate);
        if (input)
            return nlohmann::json::parse (input);
    }
    throw std::runtime_error (
      "fanout-publisher-descriptor-v1.json fixture was not found");
}

nlohmann::json read_authority_store_fixture ()
{
    std::vector<std::filesystem::path> candidates;
    auto current = std::filesystem::current_path ();
    for (int i = 0; i < 8; ++i) {
        candidates.push_back (
          current
          / "framework/testdata/location/redis/authority-store-v3.json");
        candidates.push_back (
          current
          / "testdata/location/redis/authority-store-v3.json");
        candidates.push_back (
          current
          / "../../testdata/location/redis/authority-store-v3.json");
        current = current.parent_path ();
    }
    for (const auto &candidate : candidates) {
        std::ifstream input (candidate);
        if (input)
            return nlohmann::json::parse (input);
    }
    throw std::runtime_error (
      "authority-store-v3.json fixture was not found");
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


TEST (ZLinkFrameworkLocationsRedis, StoreTypeExposesUnifiedLocationContracts)
{
    redis_location_store_t store (redis_location_options_t{
      .connection_string = "tcp://127.0.0.1:6379", .key_prefix = "zlink:test"});

    zlink::framework::location_store_t *location_store = &store;
    EXPECT_NE (nullptr, location_store);
    EXPECT_EQ ("zlink:test", store.options ().key_prefix);

    redis_relocation_store_t relocation_store (
      redis_relocation_options_t{
        .connection_string = "tcp://127.0.0.1:6379",
        .key_prefix = "zlink:test:relocations"});
    zlink::framework::relocation_store_t *relocation =
      &relocation_store;
    EXPECT_NE (nullptr, relocation);
    EXPECT_EQ ("zlink:test:relocations",
               relocation_store.options ().key_prefix);
}

TEST (ZLinkFrameworkLocationsRedis, RelocationStoreRoundTripsImmutablePayload)
{
#if !defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
    GTEST_SKIP () << "redis-plus-plus is not available in this build";
#else
    const auto location_options = find_redis_options ();
    if (!location_options)
        GTEST_SKIP () << "Redis is not reachable; set ZLINK_REDIS_TEST_ENDPOINT to enable";

    redis_relocation_store_t store (
      redis_relocation_options_t{
        .connection_string = location_options->connection_string,
        .key_prefix = location_options->key_prefix + ":relocations"});
    const std::vector<std::byte> payload{
      std::byte{0x01}, std::byte{0x7f}, std::byte{0xff}};
    const auto stored = store
                          .put_relocation (payload, std::chrono::hours (1))
                          .result ()
                          .value ();
    EXPECT_FALSE (stored.reference.empty ());
    EXPECT_GT (stored.expires_at, stored.store_now);

    const auto read = store.get_relocation (stored.reference).result ().value ();
    const auto *found =
      std::get_if<zlink::framework::relocation_found_t> (&read);
    ASSERT_NE (nullptr, found);
    EXPECT_EQ (payload, found->payload);

    const auto renewed = store
                           .renew_relocation (stored.reference,
                                              std::chrono::hours (2))
                           .result ()
                           .value ();
    EXPECT_NE (nullptr,
               std::get_if<zlink::framework::relocation_renewed_t> (
                 &renewed));
    EXPECT_EQ (zlink::framework::relocation_delete_result_t::deleted,
               store.delete_relocation (stored.reference)
                 .result ()
                 .value ());
    EXPECT_TRUE (std::holds_alternative<
                 zlink::framework::relocation_missing_t> (
      store.get_relocation (stored.reference).result ().value ()));
#endif
}

TEST (ZLinkFrameworkLocationsRedis,
      FanoutPublisherDescriptorCodecMatchesCanonicalFixture)
{
    const auto fixture =
      read_fanout_publisher_descriptor_fixture ();
    const auto &row = fixture.at ("row");
    const auto &hash = row.at ("hash");
    EXPECT_EQ (
      "fanout-publisher",
      row.at ("kind").get<std::string> ());
    EXPECT_EQ (
      "fanout-owner-a",
      hash.at ("owner").get<std::string> ());
    EXPECT_EQ (
      "5",
      hash.at ("gen").get<std::string> ());
    EXPECT_EQ (
      "1721001600000",
      hash.at ("updatedAtMs").get<std::string> ());
    EXPECT_EQ (
      "events",
      hash.at ("channel").get<std::string> ());
    const auto updated_at =
      std::chrono::system_clock::time_point{
        std::chrono::milliseconds{1721001600000}};
    const fanout_publisher_descriptor_t descriptor{
      .channel_name = "events",
      .publisher_rid =
        zlink::routing_id_t::from ("events-pub-a"),
      .lifecycle_generation = 7,
      .descriptor_revision = 3,
      .endpoint = "tcp://10.0.0.3:7500",
      .state = framework_runtime_state_t::serving,
      .security_identity = "cluster-a",
      .owner_id = "fanout-owner-a",
      .lease_generation = 5,
      .updated_at = updated_at};

    EXPECT_EQ (
      row.at ("key").get<std::string> (),
      redis_location_key_schema_t::
        encode_fanout_publisher_key (
          {descriptor.channel_name,
           descriptor.publisher_rid}));
    EXPECT_EQ (
      hash.at ("json").get<std::string> (),
      redis_location_row_codec_t::
        encode_fanout_publisher (descriptor));
    const auto decoded =
      redis_location_row_codec_t::
        decode_fanout_publisher (
          hash.at ("json").get<std::string> ());
    EXPECT_EQ (descriptor.channel_name,
               decoded.channel_name);
    EXPECT_EQ (descriptor.publisher_rid,
               decoded.publisher_rid);
    EXPECT_EQ (descriptor.lifecycle_generation,
               decoded.lifecycle_generation);
    EXPECT_EQ (descriptor.descriptor_revision,
               decoded.descriptor_revision);
    EXPECT_EQ (descriptor.endpoint, decoded.endpoint);
    EXPECT_EQ (descriptor.state, decoded.state);
    EXPECT_EQ (descriptor.security_identity,
               decoded.security_identity);
    EXPECT_EQ (descriptor.owner_id, decoded.owner_id);
    EXPECT_EQ (descriptor.lease_generation,
               decoded.lease_generation);
    EXPECT_EQ (descriptor.updated_at, decoded.updated_at);
}

TEST (ZLinkFrameworkLocationsRedis,
      ClientServerDescriptorCodecMatchesCanonicalFixture)
{
    const auto fixture =
      read_client_server_descriptor_fixture ();
    const auto &row = fixture.at ("row");
    const auto &hash = row.at ("hash");
    const auto updated_at =
      std::chrono::system_clock::time_point{
        std::chrono::milliseconds{1721001600000}};
    const client_server_server_descriptor_t descriptor{
      .channel_name = "orders",
      .server_rid =
        zlink::routing_id_t::from ("orders-a"),
      .lifecycle_generation = 7,
      .descriptor_revision = 3,
      .endpoint = "tcp://10.0.0.2:7400",
      .weight = 100,
      .state = framework_runtime_state_t::serving,
      .security_identity = "cluster-a",
      .owner_id = "channel-owner-a",
      .lease_generation = 5,
      .updated_at = updated_at};

    EXPECT_EQ (
      row.at ("key").get<std::string> (),
      redis_location_key_schema_t::
        encode_client_server_key (
          {descriptor.channel_name,
           descriptor.server_rid}));
    EXPECT_EQ (
      hash.at ("json").get<std::string> (),
      redis_location_row_codec_t::
        encode_client_server (descriptor));
    const auto decoded =
      redis_location_row_codec_t::
        decode_client_server (
          hash.at ("json").get<std::string> ());
    EXPECT_EQ (descriptor.channel_name,
               decoded.channel_name);
    EXPECT_EQ (descriptor.server_rid,
               decoded.server_rid);
    EXPECT_EQ (descriptor.lifecycle_generation,
               decoded.lifecycle_generation);
    EXPECT_EQ (descriptor.descriptor_revision,
               decoded.descriptor_revision);
    EXPECT_EQ (descriptor.endpoint, decoded.endpoint);
    EXPECT_EQ (descriptor.weight, decoded.weight);
    EXPECT_EQ (descriptor.state, decoded.state);
    EXPECT_EQ (descriptor.security_identity,
               decoded.security_identity);
    EXPECT_EQ (descriptor.owner_id, decoded.owner_id);
    EXPECT_EQ (descriptor.lease_generation,
               decoded.lease_generation);
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
      .capacity = {
        .actors = {.limit = 10000},
        .spots = {.limit = 128}},
      .activation_concurrency = {.limit = 128},
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
    EXPECT_EQ (descriptor.capacity.actors.limit,
               decoded.capacity.actors.limit);
    EXPECT_EQ (descriptor.activation_concurrency.limit,
               decoded.activation_concurrency.limit);
}


TEST (ZLinkFrameworkLocationsRedis,
      TypedCapacityProjectionRoundTripsAndAffectsImmutableDigest)
{
    using namespace zlink::framework;
    mesh_node_descriptor_t descriptor{
      .mesh_name = "play",
      .rid = zlink::routing_id_t::from ("node-a"),
      .lifecycle_generation = 1,
      .descriptor_revision = 1,
      .endpoint = "tcp://127.0.0.1:5001",
      .application_version = 1,
      .object_capabilities =
        {{.object_kind = placement_object_kind_t::user_spot,
          .stable_type = "room",
          .spot_limit = 7}},
      .object_role = object_role_t::server,
      .capacity =
        {.actors = {.active = 2, .reserved = 1, .limit = 10},
         .spots = {.active = 3, .reserved = 2, .limit = 20},
         .spot_types =
           {{.object_kind = placement_object_kind_t::user_spot,
             .stable_type = "room",
             .usage = {.active = 3, .reserved = 2, .limit = 7}}}},
      .state = framework_runtime_state_t::serving,
      .security_identity = "test",
      .owner_id = "owner-a",
      .lease_generation = 1};
    const auto encoded =
      redis_location_row_codec_t::encode_mesh_node (descriptor);
    const auto decoded =
      redis_location_row_codec_t::decode_mesh_node (encoded);
    ASSERT_EQ (1u, decoded.capacity.spot_types.size ());
    EXPECT_EQ (
      3u,
      decoded.capacity.spot_types.front ().usage.active);
    EXPECT_EQ (
      2u,
      decoded.capacity.spot_types.front ().usage.reserved);
    EXPECT_EQ (
      7,
      decoded.capacity.spot_types.front ().usage.limit);

    const auto digest =
      redis_location_row_codec_t::mesh_node_immutable_digest (
        descriptor);
    descriptor.capacity.spot_types.front ().usage.active++;
    EXPECT_EQ (
      digest,
      redis_location_row_codec_t::mesh_node_immutable_digest (
        descriptor));
    descriptor.capacity.spot_types.front ().usage.limit++;
    EXPECT_EQ (
      digest,
      redis_location_row_codec_t::mesh_node_immutable_digest (
        descriptor));
    descriptor.object_capabilities.front ().spot_limit++;
    EXPECT_NE (
      digest,
      redis_location_row_codec_t::mesh_node_immutable_digest (
        descriptor));
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
          .policy = maintenance_policy_kind_t::recreate},
         {.object_kind =
            placement_object_kind_t::user_spot,
          .stable_type = "room",
          .policy = maintenance_policy_kind_t::snapshot,
          .has_snapshot_adapter = true,
          .spot_limit = 2}},
      .object_role = object_role_t::server,
      .capacity = {
        .actors = {.limit = 5},
        .spots = {.limit = 3}},
      .activation_concurrency = {.limit = 3},
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
    EXPECT_EQ ("5", admission.at ("actorLimit"));
    EXPECT_EQ ("3", admission.at ("spotLimit"));
    EXPECT_EQ (
      "3",
      admission.at ("activationConcurrencyLimit"));
    EXPECT_EQ ("", admission.at ("entrySpotId"));
    EXPECT_EQ (64u, admission.at ("immutableDigest").size ());
    const auto capabilities = nlohmann::json::parse (
      admission.at ("capabilities"));
    ASSERT_EQ (2u, capabilities.size ());
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
      0,
      listed.items.front ()
        .object_capabilities.front ()
        .spot_limit);

    descriptor.descriptor_revision = 2;
    descriptor.capacity.actors.limit = 6;
    EXPECT_EQ (
      location_write_status_t::rejected_conflict,
      store
        .update_mesh_node (
          descriptor, location_write_intent_t::renew)
        .result ()
        .value ()
        .status);

    object_reserve_request_t creation{
      .key = {placement_object_kind_t::actor,
              "redis-profiled"},
      .intent = {.stable_type = "player"},
      .target =
        {.mesh_name = "play",
         .node_rid =
           node_rid_t::from_string ("node-descriptor"),
         .node_lifecycle_generation = 7,
         .owner = token},
      .capacity_bundle = {.actor_slots = 1}};
    creation.intent.request_content_reference =
      "inline-v1:00000000:";
    creation.intent.request_sha256[0] =
      std::byte{0x5a};
    creation.intent.request_encoded_size = 0;
    const auto reserved =
      store.reserve (creation).result ().value ();
    const auto *reserved_value =
      std::get_if<object_reserved_t> (&reserved);
    ASSERT_NE (nullptr, reserved_value);
    ASSERT_TRUE (
      reserved_value->creating.pending_creation.has_value ());
    EXPECT_EQ (
      reserved_value->fence.reservation_id,
      reserved_value->creating.pending_creation
        ->reservation_id);
    EXPECT_EQ (
      creation.intent.request_content_reference,
      reserved_value->creating.pending_creation
        ->request_content_reference);
    EXPECT_EQ (
      creation.intent.request_sha256,
      reserved_value->creating.pending_creation
        ->request_sha256);
    const auto pending_read =
      std::get<authority_snapshot_t> (
        store.read_authority (
          {"1:redis-profiled"})
          .result ()
          .value ());
    ASSERT_TRUE (pending_read.pending_creation.has_value ());
    EXPECT_EQ (
      reserved_value->fence.reservation_id,
      pending_read.pending_creation->reservation_id);
    const auto joined =
      store.reserve (creation).result ().value ();
    const auto *joined_conflict =
      std::get_if<object_reserve_conflict_t> (&joined);
    ASSERT_NE (nullptr, joined_conflict);
    const auto *joined_snapshot =
      std::get_if<authority_snapshot_t> (
        &joined_conflict->current);
    ASSERT_NE (nullptr, joined_snapshot);
    ASSERT_TRUE (
      joined_snapshot->pending_creation.has_value ());
    EXPECT_EQ (
      reserved_value->fence.reservation_id,
      joined_snapshot->pending_creation->reservation_id);
    std::vector<std::string> pending_authority_fields;
    redis.hkeys (
      redis_location_key_schema_t::authority_key (
        options->key_prefix, "1:redis-profiled"),
      std::back_inserter (pending_authority_fields));
    std::sort (
      pending_authority_fields.begin (),
      pending_authority_fields.end ());
    auto pending_fixture_fields =
      read_authority_store_fixture ()
        .at ("reservedCurrentHashFields")
        .get<std::vector<std::string>> ();
    std::sort (
      pending_fixture_fields.begin (),
      pending_fixture_fields.end ());
    EXPECT_EQ (
      pending_fixture_fields,
      pending_authority_fields);
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
    EXPECT_FALSE (
      committed_value->ready.pending_creation.has_value ());
    object_reserve_request_t spot_creation{
      .key = {placement_object_kind_t::user_spot,
              "redis-room"},
      .intent = {.stable_type = "room"},
      .target = creation.target,
      .capacity_bundle = {
        .spot_slots = 1,
        .spot_type =
          spot_type_capacity_delta_t{
            .object_kind =
              placement_object_kind_t::user_spot,
            .stable_type = "room",
            .slots = 1}}};
    const auto spot_reserved =
      std::get<object_reserved_t> (
        store.reserve (spot_creation)
          .result ()
          .value ());
    const auto actor_bucket =
      redis_location_key_schema_t::capacity_node_field (
        "play", "node-descriptor", 7,
        placement_object_kind_t::actor);
    const auto spot_bucket =
      redis_location_key_schema_t::capacity_node_field (
        "play", "node-descriptor", 7,
        placement_object_kind_t::user_spot);
    const auto room_bucket =
      redis_location_key_schema_t::capacity_type_field (
        "play", "node-descriptor", 7,
        placement_object_kind_t::user_spot, "room");
    EXPECT_EQ (
      "1",
      *redis.hget (
        redis_location_key_schema_t::
          capacity_node_active_key (
            options->key_prefix),
        actor_bucket));
    EXPECT_EQ (
      "1",
      *redis.hget (
        redis_location_key_schema_t::
          capacity_spot_reserved_key (
            options->key_prefix),
        spot_bucket));
    EXPECT_EQ (
      "1",
      *redis.hget (
        redis_location_key_schema_t::
          capacity_type_pending_key (
            options->key_prefix),
        room_bucket));
    EXPECT_NE (
      nullptr,
      std::get_if<object_aborted_t> (
        &store
           .abort (
             {spot_creation.key,
              spot_reserved.fence})
           .result ()
           .value ()));
    EXPECT_FALSE (
      redis.hget (
        redis_location_key_schema_t::
          capacity_spot_reserved_key (
            options->key_prefix),
        spot_bucket));
    EXPECT_FALSE (
      redis.hget (
        redis_location_key_schema_t::
          capacity_type_pending_key (
            options->key_prefix),
        room_bucket));
    const auto spot_reserved_again =
      std::get<object_reserved_t> (
        store.reserve (spot_creation)
          .result ()
          .value ());
    const auto spot_ready =
      std::get<object_committed_t> (
        store
          .commit (
            {spot_creation.key,
             spot_reserved_again.fence,
             {std::byte{0x31}}})
          .result ()
          .value ());
    const auto spot_deleted =
      store
        .compare_exchange_authority (
          {"2:redis-room"},
          spot_ready.ready.store_version,
          authority_delete_t{})
        .result ()
        .value ();
    ASSERT_NE (
      nullptr,
      std::get_if<authority_deleted_t> (
        &spot_deleted));
    EXPECT_FALSE (
      redis.hget (
        redis_location_key_schema_t::
          capacity_spot_active_key (
            options->key_prefix),
        spot_bucket));
    EXPECT_FALSE (
      redis.hget (
        redis_location_key_schema_t::
          capacity_type_active_key (
            options->key_prefix),
        room_bucket));
    const auto pending_revision =
      revision_hex (
        reserved_value->creating.store_version);
    const auto history_key =
      redis_location_key_schema_t::authority_history_key (
        options->key_prefix, "1:redis-profiled");
    const auto history_reservation =
      redis.hget (
        history_key,
        pending_revision + ":pendingCreationReservationId");
    ASSERT_TRUE (history_reservation);
    EXPECT_EQ (
      reserved_value->fence.reservation_id,
      *history_reservation);
    const auto history_reference =
      redis.hget (
        history_key,
        pending_revision + ":pendingCreationReference");
    ASSERT_TRUE (history_reference);
    EXPECT_EQ (
      creation.intent.request_content_reference,
      *history_reference);
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

    auto rejected_creation = creation;
    rejected_creation.key.global_id =
      "redis-terminal-rejected";
    const auto rejected_reserve =
      store.reserve (rejected_creation).result ().value ();
    const auto *rejected_fence =
      std::get_if<object_reserved_t> (
        &rejected_reserve);
    ASSERT_NE (nullptr, rejected_fence);
    const creation_operation_identity_t operation{
      node_rid_t::from_string ("node-a"), 7,
      {0x12, 0x34}};
    const auto completed_rejection =
      store
        .complete_creation (
          {rejected_creation.key,
           rejected_fence->fence,
           object_creation_rejected_t{
             {operation, {}, sha256_bytes (""),
              std::chrono::system_clock::now ()
                + std::chrono::seconds (30)}}})
        .result ()
        .value ();
    EXPECT_NE (
      nullptr,
      std::get_if<
        object_creation_completed_result_t> (
          &completed_rejection));
    const auto stored_terminal =
      store.read_creation_terminal (operation)
        .result ()
        .value ();
    ASSERT_TRUE (stored_terminal.has_value ());
    EXPECT_EQ (
      creation_terminal_state_t::rejected,
      stored_terminal->state);
    EXPECT_EQ (
      "redis-terminal-rejected",
      stored_terminal->object.global_id);
    const auto retry_reserve =
      store.reserve (rejected_creation).result ().value ();
    const auto *retry_fence =
      std::get_if<object_reserved_t> (&retry_reserve);
    ASSERT_NE (nullptr, retry_fence);
    const creation_operation_identity_t retry_operation{
      node_rid_t::from_string ("node-b"), 9,
      {0x56, 0x78}};
    const auto completed_retry =
      store
        .complete_creation (
          {rejected_creation.key,
           retry_fence->fence,
           object_creation_completed_t{
             {std::byte{0x01}},
             {retry_operation, {}, sha256_bytes (""),
              std::chrono::system_clock::now ()
                + std::chrono::seconds (30)}}})
        .result ()
        .value ();
    EXPECT_NE (
      nullptr,
      std::get_if<
        object_creation_completed_result_t> (
          &completed_retry));
    const auto retry_authority =
      store
        .read_authority (
          {"1:redis-terminal-rejected"})
        .result ()
        .value ();
    const auto *retry_ready =
      std::get_if<authority_snapshot_t> (
        &retry_authority);
    ASSERT_NE (nullptr, retry_ready);
    EXPECT_EQ (
      placement_allocation_state_t::active,
      retry_ready->allocation.state);

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
    target_descriptor.capacity.actors.limit = 2;
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
         .owner = target_token},
      .capacity_bundle = {.actor_slots = 1}};
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
    EXPECT_EQ (
      relocation_capacity_abort_result_t::aborted,
      store
        .abort_relocation_capacity (
          capacity_value->fence)
        .result ()
        .value ());

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
    aggregate.target_descriptor = {
      .mesh_name = "play",
      .rid = zlink::routing_id_t::from ("node-target")};
    aggregate.target_descriptor_lifecycle_generation = 7;
    aggregate.capacity_bundle = {.actor_slots = 1};
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
          .creating_payload = {marker},
          .capacity_bundle = {.actor_slots = 1}};
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
    auto scan_relocation = relocation;
    scan_relocation.reservation_id[14] =
      std::byte{0x46};
    scan_relocation.reservation_id[15] =
      std::byte{0x47};
    scan_relocation.key = {"1:scan-a"};
    scan_relocation.expected_store_version =
      scan_a.store_version;
    const auto scan_capacity =
      store.reserve_relocation_capacity (
        scan_relocation)
        .result ()
        .value ();
    const auto *scan_capacity_fence =
      std::get_if<relocation_capacity_reserved_t> (
        &scan_capacity);
    ASSERT_NE (nullptr, scan_capacity_fence);
    const auto scan_relocated =
      store
        .compare_exchange_authority (
          {"1:scan-a"}, scan_a.store_version,
          authority_put_t{
            {std::byte{0x21}},
            authority_generation_transition_t::new_owner,
            target_token,
            scan_capacity_fence->fence})
        .result ()
        .value ();
    ASSERT_NE (
      nullptr,
      std::get_if<authority_stored_t> (
        &scan_relocated));
    aggregate_prepare_request_t aborted_aggregate;
    aborted_aggregate.aggregate_id.value[15] =
      std::byte{0x7b};
    aborted_aggregate.aggregate_generation = 1;
    aborted_aggregate.participants = {
      {{"1:scan-a"},
       std::get<authority_stored_t> (scan_relocated)
         .snapshot.store_version,
       authority_generation_transition_t::new_owner,
       {std::byte{0x21}}, {}}};
    aborted_aggregate.target_owner = token;
    aborted_aggregate.target_descriptor = {
      .mesh_name = "play",
      .rid = zlink::routing_id_t::from (
        "node-descriptor")};
    aborted_aggregate
      .target_descriptor_lifecycle_generation = 7;
    aborted_aggregate.capacity_bundle = {
      .actor_slots = 1};
    const auto abort_prepared =
      store.prepare_aggregate (aborted_aggregate)
        .result ()
        .value ();
    const auto *abort_fence =
      std::get_if<aggregate_prepared_t> (
        &abort_prepared);
    ASSERT_NE (nullptr, abort_fence);
    EXPECT_EQ (
      aggregate_abort_result_t::aborted,
      store.abort_aggregate (abort_fence->fence)
        .result ()
        .value ());
    EXPECT_EQ (
      std::get<authority_stored_t> (scan_relocated)
        .snapshot.store_version,
      std::get<authority_snapshot_t> (
        store.read_authority ({"1:scan-a"})
          .result ()
          .value ())
        .store_version);
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



TEST (ZLinkFrameworkLocationsRedis,
      AuthorityRestorePreservesIdentityWithoutLiveOwnerLease)
{
#if !defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
    GTEST_SKIP () << "redis-plus-plus is not available in this build";
#else
    using namespace zlink::framework;
    const auto options = find_redis_options ();
    if (!options)
        GTEST_SKIP () << "Redis server is not available";
    redis_location_store_t store (*options);
    const auto owner = claim_owner (store, "restore-owner");
    const mesh_node_descriptor_t descriptor{
      .mesh_name = "play",
      .rid = zlink::routing_id_t::from ("restore-node"),
      .lifecycle_generation = 1,
      .descriptor_revision = 1,
      .endpoint = "tcp://127.0.0.1:5010",
      .application_version = 1,
      .object_capabilities =
        {{.object_kind = placement_object_kind_t::actor,
          .stable_type = "player",
          .policy = maintenance_policy_kind_t::recreate}},
      .object_role = object_role_t::server,
      .capacity = {.actors = {.limit = 4}},
      .activation_concurrency = {.limit = 4},
      .state = framework_runtime_state_t::serving,
      .security_identity = "test",
      .owner_id = owner.owner_id,
      .lease_generation = owner.lease_generation};
    ASSERT_EQ (location_write_status_t::stored,
               store.update_mesh_node (
                 descriptor, location_write_intent_t::new_claim)
                 .result ().value ().status);

    const object_reserve_request_t request{
      .key = {placement_object_kind_t::actor, "restore-actor"},
      .intent = {.stable_type = "player"},
      .target =
        {.mesh_name = "play",
         .node_rid = node_rid_t::from_string ("restore-node"),
         .node_lifecycle_generation = 1,
         .owner = owner},
      .capacity_bundle = {.actor_slots = 1}};
    const auto reserved = store.reserve (request).result ().value ();
    const auto *reservation = std::get_if<object_reserved_t> (&reserved);
    ASSERT_NE (nullptr, reservation);
    const auto committed = store
      .commit ({request.key, reservation->fence, {std::byte{0x11}}})
      .result ().value ();
    const auto *ready = std::get_if<object_committed_t> (&committed);
    ASSERT_NE (nullptr, ready);
    ASSERT_TRUE (std::holds_alternative<owner_lease_released_t> (
      store.release_owner_lease (owner).result ().value ()));

    const auto restored = store.compare_exchange_authority (
      {"1:restore-actor"}, ready->ready.store_version,
      authority_restore_t{{std::byte{0x22}}, owner})
      .result ().value ();
    const auto *stored = std::get_if<authority_stored_t> (&restored);
    ASSERT_NE (nullptr, stored);
    EXPECT_EQ ((std::vector<std::byte>{std::byte{0x22}}),
               stored->snapshot.payload);
    EXPECT_EQ (ready->ready.object_generation,
               stored->snapshot.object_generation);
    EXPECT_EQ (ready->ready.authority_owner_generation,
               stored->snapshot.authority_owner_generation);
    EXPECT_EQ (owner.owner_id, stored->snapshot.owner.owner_id);
    EXPECT_EQ (owner.lease_generation,
               stored->snapshot.owner.lease_generation);

    const auto wrong = store.compare_exchange_authority (
      {"1:restore-actor"}, stored->snapshot.store_version,
      authority_restore_t{{std::byte{0x33}}, {"other-owner", 1}})
      .result ().value ();
    EXPECT_NE (nullptr, std::get_if<authority_conflict_t> (&wrong));
#endif
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


TEST (ZLinkFrameworkLocationsRedis,
      AuthorityFixtureMatchesCommonRedisContract)
{
    const auto fixture = read_redis_location_fixture ();
    EXPECT_EQ (
      "actor-location-v2",
      fixture.at ("format").get<std::string> ());
    const auto &actor = fixture_row (fixture, "actor");
    const auto key = actor.at ("key").get<std::string> ();
    EXPECT_EQ ("zla1:a:7:actor-1", key);
    EXPECT_EQ (
      "zlink:test:{zlink-location-v3}:authority:current:"
      "5ea434456e2e59f97a9dee4d0e66dfddc0615501d5effacbd930a5eddfe55de6",
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
      "location-authority-hybrid-v3",
      fixture.at ("format").get<std::string> ());
    EXPECT_EQ (
      fixture.at ("keyContract").at ("currentKey")
        .get<std::string> (),
      redis_location_key_schema_t::authority_key (
        "P", fixture.at ("keyContract").at ("authorityKey")
               .get<std::string> ()));

    const std::vector<std::string> expected_fields{
      "authorityKey", "payload", "storeVersion",
      "objectGeneration", "authorityOwnerGeneration",
      "ownerId", "ownerLeaseGeneration", "allocationState",
      "objectKind", "stableType", "descriptorKey",
      "descriptorLifecycleGeneration", "capacityBundle"};
    EXPECT_EQ (
      expected_fields,
      fixture.at ("currentHashFields")
        .get<std::vector<std::string>> ());
    auto expected_pending_fields = expected_fields;
    expected_pending_fields.insert (
      expected_pending_fields.end (),
      {"pendingCreationReservationId",
       "pendingCreationReference",
       "pendingCreationSha256",
       "pendingCreationEncodedSize"});
    EXPECT_EQ (
      expected_pending_fields,
      fixture.at ("reservedCurrentHashFields")
        .get<std::vector<std::string>> ());
    auto expected_history_fields = expected_fields;
    expected_history_fields.insert (
      expected_history_fields.begin (), "deleted");
    EXPECT_EQ (
      expected_history_fields,
      fixture.at ("historyEncoding")
        .at ("fullSnapshotSuffixes")
        .get<std::vector<std::string>> ());
    auto expected_pending_history_fields =
      expected_pending_fields;
    expected_pending_history_fields.insert (
      expected_pending_history_fields.begin (), "deleted");
    EXPECT_EQ (
      expected_pending_history_fields,
      fixture.at ("historyEncoding")
        .at ("reservedFullSnapshotSuffixes")
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
        "game", "game-a", 7,
        zlink::framework::placement_object_kind_t::user_spot);
    const auto type = redis_location_key_schema_t::
      capacity_type_field (
        "game", "game-a", 7,
        zlink::framework::placement_object_kind_t::user_spot,
        "room");
    const auto unicode_type = redis_location_key_schema_t::
      capacity_type_field (
        "game", "game-a", 7,
        zlink::framework::placement_object_kind_t::user_spot,
        "룸.세션");
    EXPECT_EQ (
      buckets.at ("node").get<std::string> (), node);
    EXPECT_EQ (
      buckets.at ("spotType").get<std::string> (), type);
    EXPECT_EQ (
      buckets.at ("unicodeSpotType").get<std::string> (),
      unicode_type);
    EXPECT_EQ (
      "24:zlink-capacity-bundle-v21:31:11:19:user_spot4:room1:1",
      fixture.at ("capacityBundle").at ("encoded")
        .get<std::string> ());
}

TEST (ZLinkFrameworkLocationsRedis,
      ClientServerDescriptorUsesDedicatedRedisSchemaAndExactFences)
{
#if !defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
    GTEST_SKIP () << "redis-plus-plus is not available in this build";
#else
    using namespace zlink::framework;
    const auto options = find_redis_options ();
    if (!options)
        GTEST_SKIP () << "Redis server is not available";

    redis_location_store_t store (*options);
    const auto owner_a =
      std::get<owner_lease_claimed_t> (
        store
          .claim_owner_lease (
            "client-server-owner-a",
            std::chrono::seconds (10))
          .result ()
          .value ())
        .token;
    const auto owner_b =
      std::get<owner_lease_claimed_t> (
        store
          .claim_owner_lease (
            "client-server-owner-b",
            std::chrono::seconds (10))
          .result ()
          .value ())
        .token;
    const auto owner_c =
      std::get<owner_lease_claimed_t> (
        store
          .claim_owner_lease (
            "client-server-owner-c",
            std::chrono::seconds (10))
          .result ()
          .value ())
        .token;

    client_server_server_descriptor_t descriptor{
      .channel_name = "orders",
      .server_rid =
        zlink::routing_id_t::from ("orders-a"),
      .lifecycle_generation = 7,
      .descriptor_revision = 1,
      .endpoint = "tcp://127.0.0.1:7400",
      .weight = 100,
      .state = framework_runtime_state_t::serving,
      .security_identity = "cluster-a",
      .owner_id = owner_a.owner_id,
      .lease_generation =
        owner_a.lease_generation};
    const auto first_write =
      store
        .update_client_server (
          descriptor,
          location_write_intent_t::new_claim)
        .result ()
        .value ();
    ASSERT_EQ (location_write_status_t::stored,
               first_write.status);
    ASSERT_GT (first_write.generation, 0);

    const auto canonical_key =
      redis_location_key_schema_t::
        encode_client_server_key (
          {descriptor.channel_name,
           descriptor.server_rid});
    sw::redis::Redis redis (
      options->connection_string);
    const auto physical_key =
      redis_location_key_schema_t::
        client_server_key (
          options->key_prefix, canonical_key);
    std::vector<std::string> physical_fields;
    redis.hkeys (
      physical_key,
      std::back_inserter (physical_fields));
    std::sort (
      physical_fields.begin (),
      physical_fields.end ());
    auto expected_fields =
      read_client_server_descriptor_fixture ()
        .at ("hashFields")
        .get<std::vector<std::string>> ();
    std::sort (
      expected_fields.begin (),
      expected_fields.end ());
    EXPECT_EQ (expected_fields, physical_fields);

    const auto admission_key =
      redis_location_key_schema_t::
        client_server_admission_key (
          options->key_prefix, canonical_key);
    std::vector<std::pair<std::string, std::string>>
      admission_entries;
    redis.hgetall (
      admission_key,
      std::back_inserter (admission_entries));
    const std::map<std::string, std::string> admission (
      admission_entries.begin (),
      admission_entries.end ());
    EXPECT_EQ (canonical_key,
               admission.at ("descriptorKey"));
    EXPECT_EQ ("7",
               admission.at ("lifecycleGeneration"));
    EXPECT_EQ ("1",
               admission.at ("descriptorRevision"));
    EXPECT_EQ (owner_a.owner_id,
               admission.at ("ownerId"));
    EXPECT_EQ (
      std::to_string (owner_a.lease_generation),
      admission.at ("ownerLeaseGeneration"));
    EXPECT_EQ ("1", admission.at ("runtimeState"));
    EXPECT_EQ ("100", admission.at ("weight"));
    EXPECT_EQ (
      64u, admission.at ("immutableDigest").size ());
    EXPECT_TRUE (
      redis.sismember (
        redis_location_key_schema_t::
          client_server_owner_keys_key (
            options->key_prefix,
            owner_a.owner_id,
            owner_a.lease_generation),
        canonical_key));
    EXPECT_TRUE (
      static_cast<bool> (
        redis.zscore (
          redis_location_key_schema_t::
            client_server_channel_keys_key (
              options->key_prefix, "orders"),
          canonical_key)));

    auto second = descriptor;
    second.server_rid =
      zlink::routing_id_t::from ("orders-b");
    second.endpoint = "tcp://127.0.0.1:7401";
    second.owner_id = owner_c.owner_id;
    second.lease_generation =
      owner_c.lease_generation;
    ASSERT_EQ (
      location_write_status_t::stored,
      store
        .update_client_server (
          second,
          location_write_intent_t::new_claim)
        .result ()
        .value ()
        .status);

    const auto first_page =
      store
        .list_client_servers (
          "orders", {.page_size = 1})
        .result ()
        .value ();
    ASSERT_EQ (1u, first_page.items.size ());
    ASSERT_TRUE (
      first_page.continuation_token.has_value ());
    const auto second_page =
      store
        .list_client_servers (
          "orders",
          {.page_size = 1,
           .continuation_token =
             first_page.continuation_token})
        .result ()
        .value ();
    ASSERT_EQ (1u, second_page.items.size ());
    EXPECT_FALSE (
      second_page.continuation_token.has_value ());
    EXPECT_NE (first_page.items.front ().server_rid,
               second_page.items.front ().server_rid);
    EXPECT_THROW (
      store.list_client_servers (
        "other",
        {.page_size = 1,
         .continuation_token =
           first_page.continuation_token}),
      std::invalid_argument);
    auto stale_cleanup_token = owner_c;
    --stale_cleanup_token.lease_generation;
    EXPECT_EQ (
      0,
      store.remove_all_by_owner (
        stale_cleanup_token)
        .result ()
        .value ());
    EXPECT_EQ (
      2u,
      store.list_client_servers ("orders")
        .result ()
        .value ()
        .items.size ());
    EXPECT_EQ (
      1,
      store.remove_all_by_owner (owner_c)
        .result ()
        .value ());
    ASSERT_EQ (
      1u,
      store.list_client_servers ("orders")
        .result ()
        .value ()
        .items.size ());
    EXPECT_NE (
      nullptr,
      std::get_if<owner_lease_released_t> (
        &store
           .release_owner_lease (owner_c)
           .result ()
           .value ()));

    descriptor.descriptor_revision = 2;
    descriptor.weight = 50;
    EXPECT_EQ (
      location_write_status_t::stored,
      store
        .update_client_server (
          descriptor,
          location_write_intent_t::renew)
        .result ()
        .value ()
        .status);
    auto immutable_change = descriptor;
    immutable_change.descriptor_revision = 3;
    immutable_change.endpoint =
      "tcp://127.0.0.1:7499";
    EXPECT_EQ (
      location_write_status_t::ignored_stale,
      store
        .update_client_server (
          immutable_change,
          location_write_intent_t::renew)
        .result ()
        .value ()
        .status);
    auto stale_revision = descriptor;
    stale_revision.descriptor_revision = 1;
    EXPECT_EQ (
      location_write_status_t::ignored_stale,
      store
        .update_client_server (
          stale_revision,
          location_write_intent_t::renew)
        .result ()
        .value ()
        .status);

    auto takeover = descriptor;
    takeover.descriptor_revision = 1;
    takeover.owner_id = owner_b.owner_id;
    takeover.lease_generation =
      owner_b.lease_generation;
    EXPECT_EQ (
      location_write_status_t::rejected_conflict,
      store
        .update_client_server (
          takeover,
          location_write_intent_t::takeover)
        .result ()
        .value ()
        .status);
    EXPECT_NE (
      nullptr,
      std::get_if<owner_lease_released_t> (
        &store
           .release_owner_lease (owner_a)
           .result ()
           .value ()));
    EXPECT_EQ (
      location_write_status_t::stored,
      store
        .update_client_server (
          takeover,
          location_write_intent_t::takeover)
        .result ()
        .value ()
        .status);
    EXPECT_EQ (
      location_write_status_t::ignored_stale,
      store
        .remove_client_server (
          {"orders",
           zlink::routing_id_t::from ("orders-a")},
          owner_a)
        .result ()
        .value ());
    EXPECT_EQ (
      location_write_status_t::stored,
      store
        .remove_client_server (
          {"orders",
           zlink::routing_id_t::from ("orders-a")},
          owner_b)
        .result ()
        .value ());
    EXPECT_TRUE (
      store.list_client_servers ("orders")
        .result ()
        .value ()
        .items.empty ());
    EXPECT_NE (
      nullptr,
      std::get_if<owner_lease_released_t> (
        &store
           .release_owner_lease (owner_b)
           .result ()
           .value ()));
#endif
}

TEST (ZLinkFrameworkLocationsRedis,
      FanoutPublisherUsesDedicatedRedisSchemaAndExactFences)
{
#if !defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
    GTEST_SKIP () << "redis-plus-plus is not available in this build";
#else
    using namespace zlink::framework;
    const auto options = find_redis_options ();
    if (!options)
        GTEST_SKIP () << "Redis server is not available";

    redis_location_store_t store (*options);
    const auto owner_a =
      claim_owner (store, "fanout-owner-a");
    const auto owner_b =
      claim_owner (store, "fanout-owner-b");
    const auto owner_c =
      claim_owner (store, "fanout-owner-c");
    fanout_publisher_descriptor_t descriptor{
      .channel_name = "events",
      .publisher_rid =
        zlink::routing_id_t::from ("events-pub-a"),
      .lifecycle_generation = 7,
      .descriptor_revision = 1,
      .endpoint = "tcp://127.0.0.1:7500",
      .state = framework_runtime_state_t::serving,
      .security_identity = "cluster-a",
      .owner_id = owner_a.owner_id,
      .lease_generation =
        owner_a.lease_generation};
    const auto first_write =
      store
        .update_fanout_publisher (
          descriptor,
          location_write_intent_t::new_claim)
        .result ()
        .value ();
    ASSERT_EQ (location_write_status_t::stored,
               first_write.status);
    ASSERT_GT (first_write.generation, 0);

    const auto canonical_key =
      redis_location_key_schema_t::
        encode_fanout_publisher_key (
          {descriptor.channel_name,
           descriptor.publisher_rid});
    sw::redis::Redis redis (
      options->connection_string);
    const auto physical_key =
      redis_location_key_schema_t::
        fanout_publisher_key (
          options->key_prefix, canonical_key);
    std::vector<std::string> physical_fields;
    redis.hkeys (
      physical_key,
      std::back_inserter (physical_fields));
    std::sort (
      physical_fields.begin (),
      physical_fields.end ());
    auto expected_fields =
      read_fanout_publisher_descriptor_fixture ()
        .at ("hashFields")
        .get<std::vector<std::string>> ();
    std::sort (
      expected_fields.begin (),
      expected_fields.end ());
    EXPECT_EQ (expected_fields, physical_fields);

    const auto admission_key =
      redis_location_key_schema_t::
        fanout_publisher_admission_key (
          options->key_prefix, canonical_key);
    std::vector<std::pair<std::string, std::string>>
      admission_entries;
    redis.hgetall (
      admission_key,
      std::back_inserter (admission_entries));
    const std::map<std::string, std::string> admission (
      admission_entries.begin (),
      admission_entries.end ());
    EXPECT_EQ (canonical_key,
               admission.at ("descriptorKey"));
    EXPECT_EQ ("7",
               admission.at ("lifecycleGeneration"));
    EXPECT_EQ ("1",
               admission.at ("descriptorRevision"));
    EXPECT_EQ (owner_a.owner_id,
               admission.at ("ownerId"));
    EXPECT_EQ (
      std::to_string (owner_a.lease_generation),
      admission.at ("ownerLeaseGeneration"));
    EXPECT_EQ ("1", admission.at ("runtimeState"));
    EXPECT_EQ (0u, admission.count ("weight"));
    EXPECT_EQ (
      64u, admission.at ("immutableDigest").size ());
    EXPECT_TRUE (
      redis.sismember (
        redis_location_key_schema_t::
          fanout_publisher_owner_keys_key (
            options->key_prefix,
            owner_a.owner_id,
            owner_a.lease_generation),
        canonical_key));
    EXPECT_TRUE (
      static_cast<bool> (
        redis.zscore (
          redis_location_key_schema_t::
            fanout_publisher_channel_keys_key (
              options->key_prefix, "events"),
          canonical_key)));

    auto live_takeover = descriptor;
    live_takeover.lifecycle_generation = 8;
    live_takeover.endpoint =
      "tcp://127.0.0.1:7501";
    live_takeover.owner_id = owner_b.owner_id;
    live_takeover.lease_generation =
      owner_b.lease_generation;
    EXPECT_EQ (
      location_write_status_t::rejected_conflict,
      store
        .update_fanout_publisher (
          live_takeover,
          location_write_intent_t::takeover)
        .result ()
        .value ()
        .status);

    descriptor.descriptor_revision = 2;
    descriptor.state =
      framework_runtime_state_t::draining;
    EXPECT_EQ (
      location_write_status_t::stored,
      store
        .update_fanout_publisher (
          descriptor,
          location_write_intent_t::renew)
        .result ()
        .value ()
        .status);
    auto same_revision_conflict = descriptor;
    same_revision_conflict.state =
      framework_runtime_state_t::serving;
    EXPECT_EQ (
      location_write_status_t::rejected_conflict,
      store
        .update_fanout_publisher (
          same_revision_conflict,
          location_write_intent_t::renew)
        .result ()
        .value ()
        .status);
    auto immutable_change = descriptor;
    immutable_change.descriptor_revision = 3;
    immutable_change.endpoint =
      "tcp://127.0.0.1:7599";
    EXPECT_EQ (
      location_write_status_t::rejected_conflict,
      store
        .update_fanout_publisher (
          immutable_change,
          location_write_intent_t::renew)
        .result ()
        .value ()
        .status);
    auto stale_revision = descriptor;
    stale_revision.descriptor_revision = 1;
    EXPECT_EQ (
      location_write_status_t::ignored_stale,
      store
        .update_fanout_publisher (
          stale_revision,
          location_write_intent_t::renew)
        .result ()
        .value ()
        .status);

    auto second = descriptor;
    second.publisher_rid =
      zlink::routing_id_t::from ("events-pub-b");
    second.lifecycle_generation = 1;
    second.descriptor_revision = 1;
    second.endpoint = "tcp://127.0.0.1:7502";
    second.state =
      framework_runtime_state_t::serving;
    second.owner_id = owner_c.owner_id;
    second.lease_generation =
      owner_c.lease_generation;
    ASSERT_EQ (
      location_write_status_t::stored,
      store
        .update_fanout_publisher (
          second,
          location_write_intent_t::new_claim)
        .result ()
        .value ()
        .status);

    EXPECT_THROW (
      store.list_fanout_publishers (
        "events", {.page_size = 0}),
      std::invalid_argument);
    EXPECT_THROW (
      store.list_fanout_publishers (
        "events", {.page_size = 1001}),
      std::invalid_argument);
    const auto first_page =
      store
        .list_fanout_publishers (
          "events", {.page_size = 1})
        .result ()
        .value ();
    ASSERT_EQ (1u, first_page.items.size ());
    ASSERT_TRUE (
      first_page.continuation_token.has_value ());
    const auto second_page =
      store
        .list_fanout_publishers (
          "events",
          {.page_size = 1,
           .continuation_token =
             first_page.continuation_token})
        .result ()
        .value ();
    ASSERT_EQ (1u, second_page.items.size ());
    EXPECT_FALSE (
      second_page.continuation_token.has_value ());
    EXPECT_NE (
      first_page.items.front ().publisher_rid,
      second_page.items.front ().publisher_rid);
    EXPECT_THROW (
      store.list_fanout_publishers (
        "other",
        {.page_size = 1,
         .continuation_token =
           first_page.continuation_token}),
      std::invalid_argument);

    auto stale_cleanup = owner_c;
    --stale_cleanup.lease_generation;
    EXPECT_EQ (
      0,
      store.remove_all_by_owner (
        stale_cleanup)
        .result ()
        .value ());
    EXPECT_EQ (
      2u,
      store.list_fanout_publishers ("events")
        .result ()
        .value ()
        .items.size ());
    EXPECT_EQ (
      1,
      store.remove_all_by_owner (owner_c)
        .result ()
        .value ());

    EXPECT_NE (
      nullptr,
      std::get_if<owner_lease_released_t> (
        &store
           .release_owner_lease (owner_a)
           .result ()
           .value ()));
    EXPECT_EQ (
      location_write_status_t::stored,
      store
        .update_fanout_publisher (
          live_takeover,
          location_write_intent_t::takeover)
        .result ()
        .value ()
        .status);
    EXPECT_EQ (
      1u,
      store.list_fanout_publishers ("events")
        .result ()
        .value ()
        .items.size ());
    EXPECT_EQ (
      0,
      store.remove_all_by_owner (owner_a)
        .result ()
        .value ());
    EXPECT_EQ (
      location_write_status_t::ignored_stale,
      store
        .remove_fanout_publisher (
          fanout_publisher_descriptor_key_t{
            .channel_name = "events",
            .publisher_rid =
              zlink::routing_id_t::from (
                "events-pub-a")},
          owner_a)
        .result ()
        .value ());
    EXPECT_EQ (
      location_write_status_t::stored,
      store
        .remove_fanout_publisher (
          fanout_publisher_descriptor_key_t{
            .channel_name = "events",
            .publisher_rid =
              zlink::routing_id_t::from (
                "events-pub-a")},
          owner_b)
        .result ()
        .value ());
    EXPECT_TRUE (
      store.list_fanout_publishers ("events")
        .result ()
        .value ()
        .items.empty ());
    (void) store.release_owner_lease (owner_b);
    (void) store.release_owner_lease (owner_c);
#endif
}

TEST (ZLinkFrameworkLocationsRedis,
      FanoutPublisherPageStopsAtFourMiB)
{
#if !defined(ZLINK_FRAMEWORK_LOCATIONS_REDIS_HAS_ASYNC_CLIENT)
    GTEST_SKIP () << "redis-plus-plus is not available in this build";
#else
    using namespace zlink::framework;
    const auto options = find_redis_options ();
    if (!options)
        GTEST_SKIP () << "Redis server is not available";

    redis_location_store_t store (*options);
    const auto owner =
      claim_owner (store, "fanout-page-owner",
                   std::chrono::seconds (60));
    const std::string maximal_escaped_text (
      255, '\x01');
    for (std::size_t index = 0; index < 1000;
         ++index) {
        fanout_publisher_descriptor_t descriptor{
          .channel_name = maximal_escaped_text,
          .publisher_rid =
            zlink::routing_id_t::from (
              "page-publisher-"
              + std::to_string (index)),
          .lifecycle_generation = 1,
          .descriptor_revision = 1,
          .endpoint = maximal_escaped_text,
          .state =
            framework_runtime_state_t::serving,
          .security_identity =
            maximal_escaped_text,
          .owner_id = owner.owner_id,
          .lease_generation =
            owner.lease_generation};
        ASSERT_EQ (
          location_write_status_t::stored,
          store
            .update_fanout_publisher (
              std::move (descriptor),
              location_write_intent_t::new_claim)
            .result ()
            .value ()
            .status);
    }

    const auto first =
      store
        .list_fanout_publishers (
          maximal_escaped_text,
          {.page_size = 1000})
        .result ()
        .value ();
    ASSERT_FALSE (first.items.empty ());
    ASSERT_LT (first.items.size (), 1000u);
    ASSERT_TRUE (
      first.continuation_token.has_value ());
    std::size_t first_encoded_bytes = 0;
    for (const auto &descriptor : first.items)
        first_encoded_bytes +=
          redis_location_row_codec_t::
            encode_fanout_publisher (
              descriptor)
            .size ();
    EXPECT_LE (first_encoded_bytes,
               4u * 1024u * 1024u);

    const auto second =
      store
        .list_fanout_publishers (
          maximal_escaped_text,
          {.page_size = 1000,
           .continuation_token =
             first.continuation_token})
        .result ()
        .value ();
    EXPECT_EQ (
      1000u,
      first.items.size () + second.items.size ());
    EXPECT_FALSE (
      second.continuation_token.has_value ());
    EXPECT_EQ (
      1000,
      store.remove_all_by_owner (owner)
        .result ()
        .value ());
    EXPECT_TRUE (
      store
        .list_fanout_publishers (
          maximal_escaped_text)
        .result ()
        .value ()
        .items.empty ());
    (void) store.release_owner_lease (owner);
#endif
}


} // namespace
