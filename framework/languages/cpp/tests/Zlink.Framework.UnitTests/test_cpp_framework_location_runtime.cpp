/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/locations/in_memory_location_store.hpp"
#include "runtime/locations/location_lifecycle.hpp"
#include "runtime/locations/location_runtime.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{

using zlink::framework::actor_location_key_t;
using zlink::framework::actor_location_t;
using zlink::framework::location_options_t;
using zlink::framework::location_write_intent_t;
using zlink::framework::location_write_status_t;
using zlink::framework::runtime::in_memory_location_store_t;
using zlink::framework::runtime::location_runtime_t;

actor_location_t make_actor (std::string actor_id, std::int64_t generation = 0)
{
    (void) generation;
    const auto actor_id_copy = actor_id;
    return actor_location_t{.mesh_name = "play",
                            .actor_id = std::move (actor_id),
                            .actor_type = "player",
                            .actor_ref = zlink::framework::actor_ref_t (
                              zlink::framework::node_rid_t::from_string ("node-a"),
                              "player", actor_id_copy, 1),
                            .owner_node_rid = zlink::routing_id_t::from ("node-a"),
                            .owner_node_generation = 1,
                            .spot_rid = zlink::routing_id_t::from ("entry-spot"),
                            .spot_generation = 1,
                            .spot_kind = zlink::spot_kind::entry,
                            .membership_epoch = 1};
}

TEST (ZLinkFrameworkLocationRuntime, StartsOwnerLeaseBeforeWritingRows)
{
    in_memory_location_store_t store;
    location_runtime_t runtime (
      store,
      location_options_t{.heartbeat_interval = std::chrono::milliseconds (5),
                         .owner_lease_ttl = std::chrono::seconds (15)},
      "owner-a");

    runtime.start (zlink::routing_id_t::from ("node-a"));
    EXPECT_TRUE (runtime.owner_lease_healthy ());
    ASSERT_EQ (1u, store.list_owner_leases ().result ().value ().leases.size ());

    const auto write =
      runtime.write_actor (make_actor ("actor-1"), location_write_intent_t::new_claim);
    EXPECT_EQ (location_write_status_t::stored, write.status);

    auto row =
      store.resolve_actor (
             actor_location_key_t{.mesh_name = "play", .actor_id = "actor-1"})
        .result ()
        .value ();
    ASSERT_TRUE (row.has_value ());
    EXPECT_EQ ("owner-a", row->owner_id);
    EXPECT_EQ ("play", row->mesh_name);

    runtime.stop ();
    EXPECT_TRUE (store.list_owner_leases ().result ().value ().leases.empty ());
    EXPECT_FALSE (
      store.resolve_actor (
             actor_location_key_t{.mesh_name = "play", .actor_id = "actor-1"})
        .result ()
        .value ()
        .has_value ());
}

TEST (ZLinkFrameworkLocationRuntime, ReportsOwnershipLossOnIgnoredStaleWrite)
{
    in_memory_location_store_t store;
    location_runtime_t owner_a (store, {}, "owner-a");
    location_runtime_t owner_b (store, {}, "owner-b");
    owner_a.start (zlink::routing_id_t::from ("node-a"));
    owner_b.start (zlink::routing_id_t::from ("node-b"));

    const auto claimed =
      owner_a.write_actor (make_actor ("actor-1"), location_write_intent_t::new_claim);
    ASSERT_EQ (location_write_status_t::stored, claimed.status);

    const auto takeover =
      owner_b.write_actor (make_actor ("actor-1"), location_write_intent_t::takeover);
    ASSERT_EQ (location_write_status_t::stored, takeover.status);

    bool ownership_lost = false;
    std::string lost_key;
    owner_a.on_ownership_lost (
      [&] (zlink::framework::location_kind_t kind, const std::string &canonical_key) {
          if (kind == zlink::framework::location_kind_t::actor) {
              ownership_lost = true;
              lost_key = canonical_key;
          }
      });
    auto stale_actor = make_actor ("actor-1", claimed.generation);
    const auto stale = owner_a.write_actor (stale_actor, location_write_intent_t::renew);
    EXPECT_EQ (location_write_status_t::ignored_stale, stale.status);
    EXPECT_TRUE (ownership_lost);
    EXPECT_NE (std::string::npos, lost_key.find ("actor-1"));

    owner_b.stop ();
    owner_a.stop ();
}

// Regression: losing one row must deactivate only that claim. A node keeps its
// other live claims when a single actor row is taken over by another owner.
TEST (ZLinkFrameworkLocationRuntime, PerKeyOwnershipLossKeepsOtherClaims)
{
    in_memory_location_store_t store;
    location_runtime_t owner_a (store, {}, "owner-a");
    location_runtime_t owner_b (store, {}, "owner-b");
    owner_a.start (zlink::routing_id_t::from ("node-a"));
    owner_b.start (zlink::routing_id_t::from ("node-b"));
    zlink::framework::runtime::location_lifecycle_t lifecycle_a (owner_a);

    std::vector<std::string> deactivated;
    const auto record_deactivation = [&deactivated] (const actor_location_t &lost) {
        deactivated.push_back (lost.actor_id);
    };
    ASSERT_EQ (location_write_status_t::stored,
               lifecycle_a.claim_actor (make_actor ("actor-1"), record_deactivation).status);
    ASSERT_EQ (location_write_status_t::stored,
               lifecycle_a.claim_actor (make_actor ("actor-2"), record_deactivation).status);

    ASSERT_EQ (
      location_write_status_t::stored,
      owner_b.write_actor (make_actor ("actor-1"), location_write_intent_t::takeover).status);

    const auto stale = lifecycle_a.renew_actor (
      actor_location_key_t{.mesh_name = "play", .actor_id = "actor-1"});
    EXPECT_EQ (location_write_status_t::ignored_stale, stale.status);
    ASSERT_EQ (1u, deactivated.size ());
    EXPECT_EQ ("actor-1", deactivated.front ());
    EXPECT_FALSE (lifecycle_a.owns_actor (
      actor_location_key_t{.mesh_name = "play", .actor_id = "actor-1"}));
    EXPECT_TRUE (lifecycle_a.owns_actor (
      actor_location_key_t{.mesh_name = "play", .actor_id = "actor-2"}));

    owner_b.stop ();
    owner_a.stop ();
}

// Regression: releasing an actor whose row another owner already took over is an
// idempotent cleanup. It must untrack without firing the deactivation callback.
TEST (ZLinkFrameworkLocationRuntime, StaleReleaseAfterTakeoverDoesNotDeactivate)
{
    in_memory_location_store_t store;
    location_runtime_t owner_a (store, {}, "owner-a");
    location_runtime_t owner_b (store, {}, "owner-b");
    owner_a.start (zlink::routing_id_t::from ("node-a"));
    owner_b.start (zlink::routing_id_t::from ("node-b"));
    zlink::framework::runtime::location_lifecycle_t lifecycle_a (owner_a);

    std::vector<std::string> deactivated;
    const auto record_deactivation = [&deactivated] (const actor_location_t &lost) {
        deactivated.push_back (lost.actor_id);
    };
    ASSERT_EQ (location_write_status_t::stored,
               lifecycle_a.claim_actor (make_actor ("actor-1"), record_deactivation).status);
    ASSERT_EQ (location_write_status_t::stored,
               lifecycle_a.claim_actor (make_actor ("actor-2"), record_deactivation).status);

    ASSERT_EQ (
      location_write_status_t::stored,
      owner_b.write_actor (make_actor ("actor-1"), location_write_intent_t::takeover).status);

    const auto released = lifecycle_a.release_actor (
      actor_location_key_t{.mesh_name = "play", .actor_id = "actor-1"});
    EXPECT_EQ (location_write_status_t::ignored_stale, released.status);
    EXPECT_TRUE (deactivated.empty ());
    EXPECT_FALSE (lifecycle_a.owns_actor (
      actor_location_key_t{.mesh_name = "play", .actor_id = "actor-1"}));
    EXPECT_TRUE (lifecycle_a.owns_actor (
      actor_location_key_t{.mesh_name = "play", .actor_id = "actor-2"}));

    // The other owner's row survives the stale release.
    auto row = store
                 .resolve_actor (
                   actor_location_key_t{.mesh_name = "play", .actor_id = "actor-1"})
                 .result ()
                 .value ();
    ASSERT_TRUE (row.has_value ());
    EXPECT_EQ ("owner-b", row->owner_id);

    owner_b.stop ();
    owner_a.stop ();
}

} // namespace
