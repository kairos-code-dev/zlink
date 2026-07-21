/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/locations/in_memory_location_store.hpp"
#include "runtime/locations/live_location_reader.hpp"

#include <gtest/gtest.h>

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

TEST (ZLinkFrameworkInMemoryLocationStore, SharesOneStoreForAllLocationRoles)
{
    in_memory_location_store_t store;
    store.renew_owner_lease ("owner-a", zlink::routing_id_t::from ("node-1"),
                          std::chrono::seconds (15))
      .result ()
      .value ();

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

TEST (ZLinkFrameworkInMemoryLocationStore, IssuesGenerationsAndGuardsOwnerWrites)
{
    in_memory_location_store_t store;
    store
      .renew_owner_lease ("owner-a", zlink::routing_id_t::from ("node-a"),
                          std::chrono::seconds (15))
      .result ()
      .value ();
    store
      .renew_owner_lease ("owner-b", zlink::routing_id_t::from ("node-b"),
                          std::chrono::seconds (15))
      .result ()
      .value ();

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

    const auto removed = store.remove_all_by_owner ("owner-b").result ().value ();
    EXPECT_EQ (1, removed);
}

TEST (ZLinkFrameworkInMemoryLocationStore, PaginatesListsAndFiltersRows)
{
    in_memory_location_store_t store;
    store.renew_owner_lease ("owner-a", zlink::routing_id_t::from ("node-1"),
                          std::chrono::seconds (15))
      .result ()
      .value ();
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
    store
      .renew_owner_lease ("owner-a", zlink::routing_id_t::from ("node-a"),
                          std::chrono::seconds (15))
      .result ()
      .value ();

    const auto leases = store.list_owner_leases ().result ().value ();
    ASSERT_EQ (1u, leases.leases.size ());
    EXPECT_EQ ("owner-a", leases.leases.front ().owner_id);
    EXPECT_GT (leases.leases.front ().lease_expires_at, leases.store_now);

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

    EXPECT_TRUE (store.remove_owner_lease ("owner-a").result ().value ());
    EXPECT_TRUE (store.list_owner_leases ().result ().value ().leases.empty ());
}

TEST (ZLinkFrameworkInMemoryLocationStore, RemovesRowsOnlyWithMatchingOwnerToken)
{
    in_memory_location_store_t store;
    store
      .renew_owner_lease ("owner-a", zlink::routing_id_t::from ("node-a"),
                          std::chrono::seconds (15))
      .result ()
      .value ();

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
    store
      .renew_owner_lease ("owner-live", zlink::routing_id_t::from ("node-1"),
                          std::chrono::seconds (15))
      .result ()
      .value ();
    store
      .renew_owner_lease ("owner-expired", zlink::routing_id_t::from ("node-2"),
                          -std::chrono::seconds (1))
      .result ()
      .value ();

    auto live_peer = make_peer ("owner-live");
    live_peer.endpoint = "tcp://127.0.0.1:5001";
    auto expired_peer = make_peer ("owner-expired");
    expired_peer.endpoint = "tcp://127.0.0.1:5002";
    expired_peer.node_rid = zlink::routing_id_t::from ("node-2");
    store.update_peer (live_peer, location_write_intent_t::new_claim).result ().value ();
    store.update_peer (expired_peer, location_write_intent_t::new_claim).result ().value ();
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

    auto live_actor = make_actor ("owner-live", 0);
    live_actor.actor_id = "actor-live";
    auto expired_actor = make_actor ("owner-expired", 0);
    expired_actor.actor_id = "actor-expired";
    expired_actor.owner_node_rid = zlink::routing_id_t::from ("node-2");
    store.update_actor (live_actor, location_write_intent_t::new_claim).result ().value ();
    store.update_actor (expired_actor, location_write_intent_t::new_claim).result ().value ();
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

    auto live_route = make_route ("owner-live");
    auto expired_route = make_route ("owner-expired");
    expired_route.route_key = "session-expired";
    expired_route.owner_node_rid = zlink::routing_id_t::from ("node-2");
    store.update_route (live_route, location_write_intent_t::new_claim).result ().value ();
    store.update_route (expired_route, location_write_intent_t::new_claim).result ().value ();
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
