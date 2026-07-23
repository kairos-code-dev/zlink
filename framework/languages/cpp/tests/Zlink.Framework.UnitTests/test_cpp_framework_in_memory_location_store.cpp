/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/locations/in_memory_location_store.hpp"
#include "runtime/locations/live_location_reader.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace
{

using zlink::framework::actor_location_filter_t;
using zlink::framework::actor_location_key_t;
using zlink::framework::actor_location_t;
using zlink::framework::location_auto_connect_type_t;
using zlink::framework::location_change_stamp_scope_t;
using zlink::framework::location_kind_t;
using zlink::framework::location_owner_token_t;
using zlink::framework::location_page_request_t;
using zlink::framework::location_role_t;
using zlink::framework::location_write_intent_t;
using zlink::framework::location_write_status_t;
using zlink::framework::owner_lease_found_t;
using zlink::framework::owner_lease_store_t;
using zlink::framework::peer_location_filter_t;
using zlink::framework::peer_location_t;
using zlink::framework::route_kind_t;
using zlink::framework::route_location_filter_t;
using zlink::framework::route_location_key_t;
using zlink::framework::route_location_t;
using zlink::framework::spot_location_filter_t;
using zlink::framework::spot_location_key_t;
using zlink::framework::spot_location_t;
using zlink::framework::runtime::in_memory_location_store_t;
using zlink::framework::runtime::live_location_reader_t;

peer_location_t make_peer (std::string owner_id)
{
    return peer_location_t{.auto_connect_type = location_auto_connect_type_t::route_mesh,
                           .mesh_name = "play",
                           .node_rid = zlink::routing_id_t::from ("node-1"),
                           .role = location_role_t::router,
                           .endpoint = "tcp://127.0.0.1:5001",
                           .weight = 100,
                           .value = 7,
                           .owner_id = std::move (owner_id)};
}

spot_location_t make_spot (std::string owner_id, std::string spot_name = "spot-1")
{
    return spot_location_t{.mesh_name = "play",
                           .spot_rid = zlink::routing_id_t::from (spot_name),
                           .spot_type = "game",
                           .node_rid = zlink::routing_id_t::from ("node-1"),
                           .spot_kind = zlink::spot_kind::user,
                           .route_endpoint = "tcp://127.0.0.1:5001",
                           .owner_id = std::move (owner_id)};
}

actor_location_t make_actor (std::string owner_id, std::int64_t generation)
{
    (void) generation;
    return actor_location_t{
      .mesh_name = "play",
      .actor_id = "actor-1",
      .actor_type = "player",
      .actor_ref = zlink::framework::actor_ref_t (
        zlink::framework::node_rid_t::from_string ("node-1"), "player", "actor-1", 1),
      .owner_node_rid = zlink::routing_id_t::from ("node-1"),
      .owner_node_generation = 1,
      .spot_rid = zlink::routing_id_t::from ("entry-spot"),
      .spot_generation = 1,
      .spot_kind = zlink::spot_kind::entry,
      .membership_epoch = 1,
      .owner_id = std::move (owner_id)};
}

route_location_t make_route (std::string owner_id)
{
    return route_location_t{.route_kind = route_kind_t::actor_session,
                            .route_key = "session-1",
                            .owner_node_rid = zlink::routing_id_t::from ("node-1"),
                            .owner_id = std::move (owner_id),
	                            .value = {1, 2, 3}};
}

location_owner_token_t owner_token (std::string owner_id, std::int64_t generation)
{
    return location_owner_token_t{std::move (owner_id), generation};
}

location_owner_token_t claim_owner (
  in_memory_location_store_t &store,
  std::string owner_id,
  std::chrono::milliseconds ttl = std::chrono::seconds (15))
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

zlink::framework::mesh_node_descriptor_t make_mesh_node (
  std::string rid,
  location_owner_token_t owner,
  std::uint32_t pending_limit = 128)
{
    using namespace zlink::framework;
    return mesh_node_descriptor_t{
      .mesh_name = "play",
      .rid = zlink::routing_id_t::from (rid),
      .lifecycle_generation = 1,
      .descriptor_revision = 1,
      .endpoint = "tcp://127.0.0.1:5001",
      .application_version = 1,
      .object_capabilities =
        {{.object_kind = placement_object_kind_t::actor,
          .stable_type = "player",
          .policy = maintenance_policy_kind_t::recreate,
          .placement_profiles = {"standard"}}},
      .object_role = object_role_t::server,
      .object_capacity =
        {.active_limit = 100, .pending_limit = pending_limit},
      .state = framework_runtime_state_t::serving,
      .security_identity = "test",
      .owner_id = std::move (owner.owner_id),
      .lease_generation = owner.lease_generation};
}

void publish_mesh_node (
  in_memory_location_store_t &store,
  std::string rid,
  location_owner_token_t owner,
  std::uint32_t pending_limit = 128)
{
    EXPECT_EQ (
      location_write_status_t::stored,
      store
        .update_mesh_node (
          make_mesh_node (
            std::move (rid), std::move (owner),
            pending_limit),
          location_write_intent_t::new_claim)
        .result ()
        .value ()
        .status);
}

TEST (ZLinkFrameworkInMemoryLocationStore, SharesOneStoreForAllLocationRoles)
{
    in_memory_location_store_t store;
    (void) claim_owner (store, "owner-a");

    const auto peer_write =
      store.update_peer (make_peer ("owner-a"), location_write_intent_t::new_claim)
        .result ()
        .value ();
    EXPECT_EQ (location_write_status_t::stored, peer_write.status);
    EXPECT_EQ (1, peer_write.generation);

    const auto spot_write =
      store.update_spot (make_spot ("owner-a"), location_write_intent_t::new_claim)
        .result ()
        .value ();
    EXPECT_EQ (location_write_status_t::stored, spot_write.status);

    const auto route_write =
      store.update_route (make_route ("owner-a"), location_write_intent_t::new_claim)
        .result ()
        .value ();
    EXPECT_EQ (location_write_status_t::stored, route_write.status);

    const auto peers =
      store.list_peers (peer_location_filter_t{.mesh_name = "play"}).result ().value ();
    EXPECT_EQ (1u, peers.size ());

    const auto spot = store
                        .resolve_spot (spot_location_key_t{
                          .mesh_name = "play", .spot_rid = zlink::routing_id_t::from ("spot-1")})
                        .result ()
                        .value ();
    ASSERT_TRUE (spot.has_value ());
    EXPECT_EQ ("owner-a", spot->owner_id);

    const auto route = store
                         .resolve_route (route_location_key_t{
                           .route_kind = route_kind_t::actor_session, .route_key = "session-1"})
                         .result ()
                         .value ();
    ASSERT_TRUE (route.has_value ());
    EXPECT_EQ (std::vector<std::uint8_t> ({1, 2, 3}), route->value);
}

TEST (ZLinkFrameworkInMemoryLocationStore,
      AuthorityAndReservationPreserveExactOwnerFence)
{
    using namespace zlink::framework;
    in_memory_location_store_t store;
    const auto owner_a = claim_owner (store, "owner-a");
    const auto owner_b = claim_owner (store, "owner-b");
    publish_mesh_node (store, "node-a", owner_a);
    publish_mesh_node (store, "node-b", owner_b);

    object_reserve_request_t reservation{
      .key = {placement_object_kind_t::actor,
              "actor-authority"},
      .intent =
        {.stable_type = "player",
         .request_content_reference = "request-root",
         .request_encoded_size = 4},
      .target =
        {.mesh_name = "play",
         .node_rid = node_rid_t::from_string ("node-a"),
         .node_lifecycle_generation = 1,
         .owner = owner_a},
      .creating_payload = {
        std::byte{0x01}, std::byte{0x02}},
      .pending_capacity_delta = 1};
    const auto reserved =
      store.reserve (reservation).result ().value ();
    const auto *reserved_value =
      std::get_if<object_reserved_t> (&reserved);
    ASSERT_NE (nullptr, reserved_value);
    EXPECT_EQ (
      reservation.creating_payload,
      reserved_value->creating.payload);
    EXPECT_EQ (
      owner_a.lease_generation,
      reserved_value->creating.owner.lease_generation);

    const std::vector<std::byte> ready_payload{
      std::byte{0x03}, std::byte{0x04}};
    const auto committed =
      store
        .commit (
          {reservation.key, reserved_value->fence,
           ready_payload})
        .result ()
        .value ();
    const auto *committed_value =
      std::get_if<object_committed_t> (&committed);
    ASSERT_NE (nullptr, committed_value);
    EXPECT_EQ (ready_payload, committed_value->ready.payload);

    const authority_key_t authority_key{
      "1:actor-authority"};
    const auto preserved =
      store
        .compare_exchange_authority (
          authority_key,
          committed_value->ready.store_version,
          authority_put_t{
            {std::byte{0x05}},
            authority_generation_transition_t::preserve,
            std::nullopt})
        .result ()
        .value ();
    const auto *preserved_value =
      std::get_if<authority_stored_t> (&preserved);
    ASSERT_NE (nullptr, preserved_value);
    EXPECT_EQ (
      owner_a.lease_generation,
      preserved_value->snapshot.owner.lease_generation);
    EXPECT_EQ (
      committed_value->ready.authority_owner_generation,
      preserved_value->snapshot.authority_owner_generation);

    std::array<std::byte, 16> relocation_id{};
    relocation_id[15] = std::byte{0x01};
    const relocation_capacity_reserve_request_t capacity_request{
      .reservation_id = relocation_id,
      .key = authority_key,
      .expected_store_version =
        preserved_value->snapshot.store_version,
      .object_kind = placement_object_kind_t::actor,
      .stable_type = "player",
      .source = reservation.target,
      .target =
        {.mesh_name = "play",
         .node_rid = node_rid_t::from_string ("node-b"),
         .node_lifecycle_generation = 1,
         .owner = owner_b},
      .capacity_delta = 1};
    const auto capacity =
      store.reserve_relocation_capacity (capacity_request)
        .result ()
        .value ();
    const auto *capacity_value =
      std::get_if<relocation_capacity_reserved_t> (
        &capacity);
    ASSERT_NE (nullptr, capacity_value);
    const auto capacity_again =
      store.reserve_relocation_capacity (capacity_request)
        .result ()
        .value ();
    EXPECT_NE (
      nullptr,
      std::get_if<
        relocation_capacity_already_reserved_t> (
        &capacity_again));

    store.release_owner_lease (owner_a).result ().value ();
    const auto moved =
      store
        .compare_exchange_authority (
          authority_key,
          preserved_value->snapshot.store_version,
          authority_put_t{
            {std::byte{0x06}},
            authority_generation_transition_t::new_owner,
            owner_b,
            capacity_value->fence})
        .result ()
        .value ();
    const auto *moved_value =
      std::get_if<authority_stored_t> (&moved);
    ASSERT_NE (nullptr, moved_value);
    EXPECT_EQ (
      owner_b.lease_generation,
      moved_value->snapshot.owner.lease_generation);
    EXPECT_GT (
      moved_value->snapshot.authority_owner_generation,
      preserved_value->snapshot.authority_owner_generation);
    EXPECT_EQ (
      relocation_capacity_abort_result_t::already_committed,
      store
        .abort_relocation_capacity (
          capacity_value->fence)
        .result ()
        .value ());
    EXPECT_THROW (
      store.compare_exchange_authority (
        authority_key,
        moved_value->snapshot.store_version,
        authority_put_t{
          {},
          authority_generation_transition_t::preserve,
          owner_b}),
      std::invalid_argument);
}

TEST (ZLinkFrameworkInMemoryLocationStore,
      DescriptorFencesCapabilityProfileAndPendingCapacity)
{
    using namespace zlink::framework;
    in_memory_location_store_t store;
    const auto owner =
      std::get<owner_lease_claimed_t> (
        store
          .claim_owner_lease (
            "owner-target", std::chrono::seconds (15))
          .result ()
          .value ())
        .token;

    object_reserve_request_t request{
      .key = {placement_object_kind_t::actor, "profiled-a"},
      .intent =
        {.stable_type = "player",
         .placement_profile =
           placement_profile_t{"standard"}},
      .target =
        {.mesh_name = "play",
         .node_rid = node_rid_t::from_string ("node-target"),
         .node_lifecycle_generation = 1,
         .owner = owner}};
    EXPECT_NE (
      nullptr,
      std::get_if<object_reserve_conflict_t> (
        &store.reserve (request).result ().value ()));

    publish_mesh_node (
      store, "node-target", owner, 1);
    auto wrong_profile = request;
    wrong_profile.key.global_id = "profiled-wrong";
    wrong_profile.intent.placement_profile =
      placement_profile_t{"premium"};
    EXPECT_NE (
      nullptr,
      std::get_if<object_reserve_conflict_t> (
        &store.reserve (wrong_profile).result ().value ()));

    EXPECT_NE (
      nullptr,
      std::get_if<object_reserved_t> (
        &store.reserve (request).result ().value ()));
    auto over_limit = request;
    over_limit.key.global_id = "profiled-b";
    EXPECT_NE (
      nullptr,
      std::get_if<object_placement_capacity_exhausted_t> (
        &store.reserve (over_limit).result ().value ()));

    auto changed =
      make_mesh_node ("node-target", owner, 2);
    changed.descriptor_revision = 2;
    EXPECT_EQ (
      location_write_status_t::rejected_conflict,
      store
        .update_mesh_node (
          changed, location_write_intent_t::renew)
        .result ()
        .value ()
        .status);
    const auto listed =
      store.list_mesh_nodes ("play").result ().value ();
    ASSERT_EQ (1u, listed.items.size ());
    EXPECT_EQ (
      1u, listed.items.front ().object_capacity.pending_limit);
}

TEST (ZLinkFrameworkInMemoryLocationStore,
      StoreRevisionExhaustionDoesNotReuseVersionOrMutateAuthority)
{
    using namespace zlink::framework;
    const auto max_revision =
      static_cast<std::uint64_t> (
        std::numeric_limits<std::int64_t>::max ());
    in_memory_location_store_t store{max_revision - 2};
    const auto owner =
      std::get<owner_lease_claimed_t> (
        store
          .claim_owner_lease (
            "owner-max", std::chrono::seconds (15))
          .result ()
          .value ())
        .token;
    publish_mesh_node (store, "node-max", owner);
    object_reserve_request_t request{
      .key = {placement_object_kind_t::actor, "max-revision"},
      .intent = {.stable_type = "player"},
      .target =
        {.mesh_name = "play",
         .node_rid = node_rid_t::from_string ("node-max"),
         .node_lifecycle_generation = 1,
         .owner = owner}};
    const auto reserved =
      std::get<object_reserved_t> (
        store.reserve (request).result ().value ());
    const auto committed =
      std::get<object_committed_t> (
        store
          .commit (
            {request.key, reserved.fence, {std::byte{0x01}}})
          .result ()
          .value ());
    ASSERT_EQ (std::to_string (max_revision),
               committed.ready.store_version);

    const auto result =
      store
        .compare_exchange_authority (
          { "1:max-revision" },
          committed.ready.store_version,
          authority_put_t{
            {std::byte{0x02}},
            authority_generation_transition_t::preserve,
            std::nullopt})
        .result ()
        .value ();
    EXPECT_NE (
      nullptr,
      std::get_if<authority_generation_exhausted_t> (&result));
    const auto current =
      std::get<authority_snapshot_t> (
        store
          .read_authority ({"1:max-revision"})
          .result ()
          .value ());
    EXPECT_EQ (committed.ready.store_version,
               current.store_version);
    EXPECT_EQ (committed.ready.payload, current.payload);
}

TEST (ZLinkFrameworkInMemoryLocationStore,
      AggregateRequiresExactRelocationCapacityFenceSet)
{
    using namespace zlink::framework;
    in_memory_location_store_t store;
    const auto owner_a = claim_owner (store, "owner-a");
    const auto owner_b = claim_owner (store, "owner-b");
    publish_mesh_node (store, "node-a", owner_a);
    publish_mesh_node (store, "node-b", owner_b);

    const auto create =
      [&] (std::string id, std::byte marker)
        -> authority_snapshot_t {
        object_reserve_request_t request{
          .key = {placement_object_kind_t::actor, id},
          .intent = {.stable_type = "player"},
          .target =
            {.mesh_name = "play",
             .node_rid = node_rid_t::from_string ("node-a"),
             .node_lifecycle_generation = 1,
             .owner = owner_a},
          .creating_payload = {marker}};
        const auto reserved =
          store.reserve (request).result ().value ();
        const auto &fence =
          std::get<object_reserved_t> (reserved).fence;
        return std::get<object_committed_t> (
                 store
                   .commit (
                     {request.key, fence, {marker}})
                   .result ()
                   .value ())
          .ready;
      };
    const auto first = create ("aggregate-a", std::byte{0x11});
    const auto second = create ("aggregate-b", std::byte{0x12});
    const auto extra = create ("aggregate-c", std::byte{0x13});

    const auto reserve_capacity =
      [&] (std::uint8_t id,
           std::string key,
           const authority_snapshot_t &snapshot)
        -> relocation_capacity_fence_t {
        std::array<std::byte, 16> reservation_id{};
        reservation_id[15] = static_cast<std::byte> (id);
        relocation_capacity_reserve_request_t request{
          .reservation_id = reservation_id,
          .key = {std::move (key)},
          .expected_store_version = snapshot.store_version,
          .object_kind = placement_object_kind_t::actor,
          .stable_type = "player",
          .source =
            {.mesh_name = "play",
             .node_rid = node_rid_t::from_string ("node-a"),
             .node_lifecycle_generation = 1,
             .owner = owner_a},
          .target =
            {.mesh_name = "play",
             .node_rid = node_rid_t::from_string ("node-b"),
             .node_lifecycle_generation = 1,
             .owner = owner_b}};
        return std::get<relocation_capacity_reserved_t> (
                 store
                   .reserve_relocation_capacity (
                     std::move (request))
                   .result ()
                   .value ())
          .fence;
      };
    const auto first_fence =
      reserve_capacity (1, "1:aggregate-a", first);
    const auto second_fence =
      reserve_capacity (2, "1:aggregate-b", second);
    const auto extra_fence =
      reserve_capacity (3, "1:aggregate-c", extra);

    aggregate_prepare_request_t request;
    request.aggregate_id.value[15] = std::byte{0x21};
    request.aggregate_generation = 1;
    request.participants = {
      {{ "1:aggregate-a" },
       first.store_version,
       authority_generation_transition_t::new_owner,
       {std::byte{0x31}},
       {}},
      {{ "1:aggregate-b" },
       second.store_version,
       authority_generation_transition_t::new_owner,
       {std::byte{0x32}},
       {}}};
    request.target_owner = owner_b;

    request.target_reservations = {first_fence};
    EXPECT_NE (
      nullptr,
      std::get_if<aggregate_prepare_conflict_t> (
        &store.prepare_aggregate (request).result ().value ()));
    request.target_reservations = {first_fence, first_fence};
    EXPECT_NE (
      nullptr,
      std::get_if<aggregate_prepare_conflict_t> (
        &store.prepare_aggregate (request).result ().value ()));
    request.target_reservations = {
      first_fence, second_fence, extra_fence};
    EXPECT_NE (
      nullptr,
      std::get_if<aggregate_prepare_conflict_t> (
        &store.prepare_aggregate (request).result ().value ()));
    EXPECT_EQ (
      first.store_version,
      std::get<authority_snapshot_t> (
        store.read_authority ({"1:aggregate-a"})
          .result ()
          .value ())
        .store_version);
    EXPECT_EQ (
      second.store_version,
      std::get<authority_snapshot_t> (
        store.read_authority ({"1:aggregate-b"})
          .result ()
          .value ())
        .store_version);

    request.target_reservations = {
      first_fence, second_fence};
    store.release_owner_lease (owner_a).result ().value ();
    const auto prepared =
      store.prepare_aggregate (request).result ().value ();
    const auto *prepared_value =
      std::get_if<aggregate_prepared_t> (&prepared);
    ASSERT_NE (nullptr, prepared_value);
    EXPECT_EQ (
      relocation_capacity_abort_result_t::stale,
      store.abort_relocation_capacity (first_fence)
        .result ()
        .value ());
    EXPECT_EQ (
      aggregate_commit_result_t::committed,
      store.commit_aggregate (prepared_value->fence)
        .result ()
        .value ());
    EXPECT_EQ (
      owner_b.lease_generation,
      std::get<authority_snapshot_t> (
        store.read_authority ({"1:aggregate-a"})
          .result ()
          .value ())
        .owner.lease_generation);
    EXPECT_EQ (
      owner_b.lease_generation,
      std::get<authority_snapshot_t> (
        store.read_authority ({"1:aggregate-b"})
          .result ()
          .value ())
        .owner.lease_generation);
}

TEST (ZLinkFrameworkInMemoryLocationStore, IssuesGenerationsAndGuardsOwnerWrites)
{
    in_memory_location_store_t store;
    (void) claim_owner (store, "owner-a");
    (void) claim_owner (store, "owner-b");

    const auto claimed =
      store.update_actor (make_actor ("owner-a", 0), location_write_intent_t::new_claim)
        .result ()
        .value ();
    EXPECT_EQ (location_write_status_t::stored, claimed.status);
    EXPECT_EQ (1, claimed.generation);

    const auto conflict =
      store.update_actor (make_actor ("owner-b", 0), location_write_intent_t::new_claim)
        .result ()
        .value ();
    EXPECT_EQ (location_write_status_t::rejected_conflict, conflict.status);

    const auto renewed =
      store
        .update_actor (make_actor ("owner-a", claimed.generation), location_write_intent_t::renew)
        .result ()
        .value ();
    EXPECT_EQ (location_write_status_t::stored, renewed.status);
    EXPECT_EQ (claimed.generation, renewed.generation);

    const auto takeover =
      store.update_actor (make_actor ("owner-b", 0), location_write_intent_t::takeover)
        .result ()
        .value ();
    EXPECT_EQ (location_write_status_t::stored, takeover.status);
    EXPECT_EQ (2, takeover.generation);

    const auto stale =
      store
        .update_actor (make_actor ("owner-a", claimed.generation), location_write_intent_t::renew)
        .result ()
        .value ();
    EXPECT_EQ (location_write_status_t::ignored_stale, stale.status);

    const auto owner_b =
      std::get<owner_lease_found_t> (
        store.read_owner_lease ("owner-b")
          .result ()
          .value ())
        .token;
    const auto removed =
      store.remove_all_by_owner (owner_b)
        .result ()
        .value ();
    EXPECT_EQ (1, removed);
}

TEST (ZLinkFrameworkInMemoryLocationStore, PaginatesListsAndFiltersRows)
{
    in_memory_location_store_t store;
    (void) claim_owner (store, "owner-a");
    store.update_spot (make_spot ("owner-a", "spot-1"), location_write_intent_t::new_claim)
      .result ()
      .value ();
    store.update_spot (make_spot ("owner-a", "spot-2"), location_write_intent_t::new_claim)
      .result ()
      .value ();

    const auto first_page = store
                              .list_spots (spot_location_filter_t{.mesh_name = "play"},
                                           location_page_request_t{.page_size = 1})
                              .result ()
                              .value ();
    ASSERT_EQ (1u, first_page.items.size ());
    ASSERT_TRUE (first_page.continuation_token.has_value ());

    const auto second_page =
      store
        .list_spots (spot_location_filter_t{.mesh_name = "play"},
                     location_page_request_t{.page_size = 1,
                                             .continuation_token = first_page.continuation_token})
        .result ()
        .value ();
    EXPECT_EQ (1u, second_page.items.size ());
    EXPECT_FALSE (second_page.continuation_token.has_value ());

    store.update_actor (make_actor ("owner-a", 0), location_write_intent_t::new_claim)
      .result ()
      .value ();
    const auto actors =
      store.list_actors (actor_location_filter_t{.actor_type = "player"}).result ().value ();
    EXPECT_EQ (1u, actors.items.size ());

    store.update_route (make_route ("owner-a"), location_write_intent_t::new_claim)
      .result ()
      .value ();
    const auto routes =
      store.list_routes (route_location_filter_t{.route_kind = route_kind_t::actor_session})
        .result ()
        .value ();
    EXPECT_EQ (1u, routes.items.size ());
}

TEST (ZLinkFrameworkInMemoryLocationStore, MaintainsOwnerLeasesAndChangeStamps)
{
    in_memory_location_store_t store;
    const auto owner = claim_owner (store, "owner-a");
    const auto lease =
      store.read_owner_lease ("owner-a").result ().value ();
    const auto *found =
      std::get_if<owner_lease_found_t> (&lease);
    ASSERT_NE (nullptr, found);
    EXPECT_EQ (owner.lease_generation,
               found->token.lease_generation);
    EXPECT_GT (found->lease_expires_at, found->store_now);

    const auto before =
      store.get_change_stamp (location_change_stamp_scope_t{location_kind_t::peer, "play"})
        .result ()
        .value ();
    store.update_peer (make_peer ("owner-a"), location_write_intent_t::new_claim)
      .result ()
      .value ();
    const auto after =
      store.get_change_stamp (location_change_stamp_scope_t{location_kind_t::peer, "play"})
        .result ()
        .value ();
    EXPECT_EQ (before + 1, after);

    const auto released =
      store.release_owner_lease (owner).result ().value ();
    EXPECT_TRUE (
      std::holds_alternative<
        zlink::framework::owner_lease_released_t> (
        released));
    EXPECT_TRUE (
      std::holds_alternative<
        zlink::framework::owner_lease_missing_t> (
        store.read_owner_lease ("owner-a")
          .result ()
          .value ()));
}

TEST (ZLinkFrameworkInMemoryLocationStore, RemovesRowsOnlyWithMatchingOwnerToken)
{
    in_memory_location_store_t store;
    (void) claim_owner (store, "owner-a");

    const auto peer_claim =
      store.update_peer (make_peer ("owner-a"), location_write_intent_t::new_claim)
        .result ()
        .value ();
    const auto stale_peer = store
                              .remove_peer (zlink::framework::peer_location_key_t{
                                             .auto_connect_type =
                                               location_auto_connect_type_t::route_mesh,
                                             .mesh_name = "play",
                                             .role = location_role_t::router,
                                             .node_rid = zlink::routing_id_t::from ("node-1"),
                                             .endpoint = "tcp://127.0.0.1:5001"},
                                            owner_token ("owner-b", peer_claim.generation))
                              .result ()
                              .value ();
    EXPECT_EQ (location_write_status_t::ignored_stale, stale_peer.status);
    EXPECT_EQ (1u, store.list_peers ({}).result ().value ().size ());
    const auto removed_peer = store
                                .remove_peer (zlink::framework::peer_location_key_t{
                                               .auto_connect_type =
                                                 location_auto_connect_type_t::route_mesh,
                                               .mesh_name = "play",
                                               .role = location_role_t::router,
                                               .node_rid = zlink::routing_id_t::from ("node-1"),
                                               .endpoint = "tcp://127.0.0.1:5001"},
                                             owner_token ("owner-a", peer_claim.generation))
                                .result ()
                                .value ();
    EXPECT_EQ (location_write_status_t::stored, removed_peer.status);
    EXPECT_TRUE (store.list_peers ({}).result ().value ().empty ());

    const auto spot_claim =
      store.update_spot (make_spot ("owner-a"), location_write_intent_t::new_claim)
        .result ()
        .value ();
    EXPECT_EQ (location_write_status_t::stored,
               store
                 .remove_spot (spot_location_key_t{.mesh_name = "play",
                                                   .spot_rid = zlink::routing_id_t::from ("spot-1")},
                               owner_token ("owner-a", spot_claim.generation))
                 .result ()
                 .value ()
                 .status);
    EXPECT_FALSE (store
                    .resolve_spot (spot_location_key_t{
                      .mesh_name = "play", .spot_rid = zlink::routing_id_t::from ("spot-1")})
                    .result ()
                    .value ()
                    .has_value ());

    const auto actor_claim =
      store.update_actor (make_actor ("owner-a", 0), location_write_intent_t::new_claim)
        .result ()
        .value ();
    EXPECT_EQ (location_write_status_t::stored,
               store
                 .remove_actor (actor_location_key_t{.mesh_name = "play", .actor_id = "actor-1"},
                                owner_token ("owner-a", actor_claim.generation))
                 .result ()
                 .value ()
                 .status);
    EXPECT_FALSE (store
                    .resolve_actor (
                      actor_location_key_t{.mesh_name = "play", .actor_id = "actor-1"})
                    .result ()
                    .value ()
                    .has_value ());

    const auto route_claim =
      store.update_route (make_route ("owner-a"), location_write_intent_t::new_claim)
        .result ()
        .value ();
    EXPECT_EQ (location_write_status_t::stored,
               store
                 .remove_route (route_location_key_t{.route_kind = route_kind_t::actor_session,
                                                     .route_key = "session-1"},
                                owner_token ("owner-a", route_claim.generation))
                 .result ()
                 .value ()
                 .status);
    EXPECT_FALSE (store
                    .resolve_route (route_location_key_t{.route_kind = route_kind_t::actor_session,
                                                        .route_key = "session-1"})
                    .result ()
                    .value ()
                    .has_value ());
}

TEST (ZLinkFrameworkInMemoryLocationStore, FiltersRowsAndHidesExpiredOwners)
{
    in_memory_location_store_t store;
    live_location_reader_t live (store);
    (void) claim_owner (store, "owner-live");
    const auto expired_owner =
      claim_owner (store, "owner-expired");

    auto live_peer = make_peer ("owner-live");
    live_peer.endpoint = "tcp://127.0.0.1:5001";
    auto expired_peer = make_peer ("owner-expired");
    expired_peer.endpoint = "tcp://127.0.0.1:5002";
    expired_peer.node_rid = zlink::routing_id_t::from ("node-2");
    store.update_peer (live_peer, location_write_intent_t::new_claim).result ().value ();
    store.update_peer (expired_peer, location_write_intent_t::new_claim).result ().value ();
    auto live_actor = make_actor ("owner-live", 0);
    live_actor.actor_id = "actor-live";
    auto expired_actor = make_actor ("owner-expired", 0);
    expired_actor.actor_id = "actor-expired";
    expired_actor.owner_node_rid = zlink::routing_id_t::from ("node-2");
    store.update_actor (live_actor, location_write_intent_t::new_claim).result ().value ();
    store.update_actor (expired_actor, location_write_intent_t::new_claim).result ().value ();
    auto live_route = make_route ("owner-live");
    auto expired_route = make_route ("owner-expired");
    expired_route.route_key = "session-expired";
    expired_route.owner_node_rid = zlink::routing_id_t::from ("node-2");
    store.update_route (live_route, location_write_intent_t::new_claim).result ().value ();
    store.update_route (expired_route, location_write_intent_t::new_claim).result ().value ();
    (void) store.release_owner_lease (expired_owner)
      .result ()
      .value ();

    EXPECT_EQ (1u, live.list_peers (peer_location_filter_t{.node_rid =
                                                             zlink::routing_id_t::from ("node-1")})
                     .result ()
                     .value ()
                     .size ());
    EXPECT_TRUE (live.list_peers (peer_location_filter_t{.node_rid =
                                                           zlink::routing_id_t::from ("node-2")})
                   .result ()
                   .value ()
                   .empty ());

    EXPECT_EQ (1u, live
                     .list_actors (actor_location_filter_t{.owner_node_rid =
                                                             zlink::routing_id_t::from ("node-1")})
                     .result ()
                     .value ()
                     .items.size ());
    EXPECT_TRUE (live
                   .list_actors (actor_location_filter_t{.owner_node_rid =
                                                           zlink::routing_id_t::from ("node-2")})
                   .result ()
                   .value ()
                   .items.empty ());

    EXPECT_EQ (1u, live
                     .list_routes (route_location_filter_t{.owner_node_rid =
                                                             zlink::routing_id_t::from ("node-1")})
                     .result ()
                     .value ()
                     .items.size ());
    EXPECT_TRUE (live
                   .list_routes (route_location_filter_t{.owner_id = "owner-expired"})
                   .result ()
                   .value ()
                   .items.empty ());

    const auto invalid_page =
      store
        .list_routes (route_location_filter_t{.route_kind = route_kind_t::actor_session},
                      location_page_request_t{.page_size = 1, .continuation_token = "not-a-number"})
        .result ()
        .value ();
    EXPECT_EQ (1u, invalid_page.items.size ());
}

} // namespace
