/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/locations/in_memory_location_store.hpp"
#include "runtime/locations/location_runtime.hpp"

#include <gtest/gtest.h>

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
    return actor_location_t{.actor_id = std::move (actor_id),
                            .actor_type = "player",
                            .actor_ref = std::nullopt,
                            .node_rid = zlink::routing_id_t::from ("node-a"),
                            .location_kind = zlink::spot_kind::entry,
                            .spot_mesh_name = "play",
                            .generation = generation};
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
      store.resolve_actor (actor_location_key_t{.actor_id = "actor-1"})
        .result ()
        .value ();
    ASSERT_TRUE (row.has_value ());
    EXPECT_EQ ("owner-a", row->owner_id);
    EXPECT_EQ (write.generation, row->generation);

    runtime.stop ();
    EXPECT_TRUE (store.list_owner_leases ().result ().value ().leases.empty ());
    EXPECT_FALSE (
      store.resolve_actor (actor_location_key_t{.actor_id = "actor-1"})
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
    owner_a.on_ownership_lost ([&] { ownership_lost = true; });
    auto stale_actor = make_actor ("actor-1", claimed.generation);
    const auto stale = owner_a.write_actor (stale_actor, location_write_intent_t::renew);
    EXPECT_EQ (location_write_status_t::ignored_stale, stale.status);
    EXPECT_TRUE (ownership_lost);

    owner_b.stop ();
    owner_a.stop ();
}

} // namespace
