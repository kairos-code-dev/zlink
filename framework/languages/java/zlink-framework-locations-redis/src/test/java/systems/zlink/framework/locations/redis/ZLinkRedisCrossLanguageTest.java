package systems.zlink.framework.locations.redis;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkActorLocationKey;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.locations.ZLinkLocationWriteStatus;
import systems.zlink.framework.locations.ZLinkOwnerLeaseClaimed;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkPeerLocationFilter;
import systems.zlink.framework.locations.ZLinkRouteKind;
import systems.zlink.framework.locations.ZLinkRouteLocation;
import systems.zlink.framework.locations.ZLinkRouteLocationKey;
import systems.zlink.framework.locations.ZLinkSpotLocation;
import systems.zlink.framework.locations.ZLinkSpotLocationKey;
import systems.zlink.framework.spots.ZLinkSpotKind;

final class ZLinkRedisCrossLanguageTest {
    private static final Instant UPDATED_AT = Instant.parse("2026-07-03T00:00:00Z");
    private static final Duration LEASE_TTL = Duration.ofSeconds(30);

    @Test
    void javaWritesRowsForDotnetToRead() throws Exception {
        try (ZLinkRedisLocationStore store = crossLanguageStore("java")) {
            assertNotNull(((ZLinkOwnerLeaseClaimed)
                store.claimOwnerLease("java-owner", LEASE_TTL)
                    .toCompletableFuture()
                    .get()).token());

            assertEquals(ZLinkLocationWriteStatus.STORED,
                store.updatePeer(javaPeer(), ZLinkLocationWriteIntent.NEW_CLAIM)
                    .toCompletableFuture()
                    .get()
                    .status());
            assertEquals(ZLinkLocationWriteStatus.STORED,
                store.updateSpot(javaSpot(), ZLinkLocationWriteIntent.NEW_CLAIM)
                    .toCompletableFuture()
                    .get()
                    .status());
            assertEquals(ZLinkLocationWriteStatus.STORED,
                store.updateActor(javaActor(), ZLinkLocationWriteIntent.NEW_CLAIM)
                    .toCompletableFuture()
                    .get()
                    .status());
            assertEquals(ZLinkLocationWriteStatus.STORED,
                store.updateRoute(javaRoute(), ZLinkLocationWriteIntent.NEW_CLAIM)
                    .toCompletableFuture()
                    .get()
                    .status());
        }
    }

    @Test
    void javaReadsRowsWrittenByDotnet() throws Exception {
        try (ZLinkRedisLocationStore store = crossLanguageStore("dotnet")) {
            ZLinkActorLocation actor = store.resolveActor(
                    new ZLinkActorLocationKey("dotnet-actor"))
                .toCompletableFuture()
                .get();
            ZLinkSpotLocation spot = store.resolveSpot(
                    new ZLinkSpotLocationKey("dotnet-spot"))
                .toCompletableFuture()
                .get();
            ZLinkRouteLocation route = store.resolveRoute(
                    new ZLinkRouteLocationKey(ZLinkRouteKind.ACTOR_SESSION, "dotnet-route"))
                .toCompletableFuture()
                .get();
            List<ZLinkPeerLocation> peers = store.listPeerLocations(new ZLinkPeerLocationFilter(
                    ZLinkLocationAutoConnectType.ROUTE_MESH,
                    "cross",
                    ZLinkLocationRole.ROUTER,
                    RoutingId.from("dotnet-node"),
                    null))
                .toCompletableFuture()
                .get();

            assertNotNull(actor);
            assertEquals(new ActorRef(RoutingId.from("dotnet-node"), "dotnet-actor", 0), actor.actorRef());
            assertEquals(RoutingId.from("dotnet-node"), actor.nodeRid());
            assertEquals("dotnet-owner", actor.ownerId());

            assertNotNull(spot);
            assertEquals("dotnet-game", spot.spotType());
            assertEquals(RoutingId.from("dotnet-node"), spot.nodeRid());

            assertNotNull(route);
            assertArrayEquals(new byte[] {9, 8, 7, 6}, route.value());

            ZLinkPeerLocation peer = peers.stream()
                .filter(row -> "tcp://127.0.0.1:5310".equals(row.endpoint()))
                .findFirst()
                .orElse(null);
            assertNotNull(peer);
            assertEquals("tcp://127.0.0.1:6310", peer.metadata().get("route-endpoint"));
            assertEquals(List.of("dotnet", "route"), peer.capabilities());
        }
    }

    private static ZLinkRedisLocationStore crossLanguageStore(String language) {
        String endpoint = System.getenv("ZLINK_REDIS_LOCATION_ENDPOINT");
        String prefix = System.getenv("ZLINK_REDIS_CROSS_LANGUAGE_PREFIX");
        assumeTrue(endpoint != null && !endpoint.isBlank(), "ZLINK_REDIS_LOCATION_ENDPOINT is not set");
        assumeTrue(prefix != null && !prefix.isBlank(), "ZLINK_REDIS_CROSS_LANGUAGE_PREFIX is not set");
        return new ZLinkRedisLocationStore(new ZLinkRedisLocationOptions()
            .setConnectionString(endpoint)
            .setKeyPrefix(prefix + ":" + language));
    }

    private static ZLinkPeerLocation javaPeer() {
        return new ZLinkPeerLocation(
            ZLinkLocationAutoConnectType.ROUTE_MESH,
            "cross",
            RoutingId.from("java-node"),
            ZLinkLocationRole.ROUTER,
            "tcp://127.0.0.1:5300",
            100,
            false,
            7,
            Map.of("route-endpoint", "tcp://127.0.0.1:6300"),
            List.of("java", "route"),
            "java-owner",
            0,
            UPDATED_AT);
    }

    private static ZLinkSpotLocation javaSpot() {
        return new ZLinkSpotLocation(
            "cross",
            "java-spot",
            "java-game",
            RoutingId.from("java-node"),
            ZLinkSpotKind.USER,
            "tcp://127.0.0.1:5300",
            "java-owner",
            0,
            UPDATED_AT);
    }

    private static ZLinkActorLocation javaActor() {
        return new ZLinkActorLocation(
            "java-actor",
            "player",
            new ActorRef(RoutingId.from("java-node"), "java-actor", 0),
            RoutingId.from("java-node"),
            ZLinkSpotKind.USER,
            "cross",
            "java-spot",
            "java-owner",
            0,
            UPDATED_AT);
    }

    private static ZLinkRouteLocation javaRoute() {
        return new ZLinkRouteLocation(
            ZLinkRouteKind.ACTOR_SESSION,
            "java-route",
            RoutingId.from("java-node"),
            "java-owner",
            0,
            new byte[] {1, 2, 3, 4},
            UPDATED_AT);
    }
}
