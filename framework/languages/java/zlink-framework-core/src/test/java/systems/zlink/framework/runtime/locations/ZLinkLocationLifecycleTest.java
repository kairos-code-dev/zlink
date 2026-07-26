package systems.zlink.framework.runtime.locations;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.time.ZoneOffset;
import java.util.concurrent.CompletionException;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.locations.ZLinkActorLocationKey;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.locations.ZLinkLocationWriteStatus;
import systems.zlink.framework.locations.ZLinkPageRequest;
import systems.zlink.framework.locations.ZLinkRouteKind;
import systems.zlink.framework.locations.ZLinkRouteLocationFilter;
import systems.zlink.framework.locations.ZLinkRouteLocationKey;
import systems.zlink.framework.spots.ZLinkSpotKind;

class ZLinkLocationLifecycleTest {
    private static final Instant NOW = Instant.parse("2026-07-03T00:00:00Z");
    private static final RoutingId NODE_A = RoutingId.from("node-a");
    private static final RoutingId NODE_B = RoutingId.from("node-b");
    private static final RoutingId SPOT_RID = RoutingId.from("spot-a");

    @Test
    void startRenewsLeaseBeforeLifecycleClaimWritesRows() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore(Clock.fixed(NOW, ZoneOffset.UTC));
        ZLinkLocationRuntime runtime = newRuntime(store, "owner-a");
        try (runtime; ZLinkLocationLifecycle lifecycle = new ZLinkLocationLifecycle(runtime)) {
            runtime.start(NODE_A).toCompletableFuture().get();

            ZLinkLocationWriteStatus status = lifecycle.claimSpot(
                    "mesh", SPOT_RID.toString(), "room", NODE_A, ZLinkSpotKind.USER, "tcp://127.0.0.1:6000", null)
                .toCompletableFuture()
                .get();

            assertEquals(ZLinkLocationWriteStatus.STORED, status);
            assertTrue(runtime.ownerLeaseHealthy());
            assertEquals("owner-a", store.resolveSpot(new systems.zlink.framework.locations.ZLinkSpotLocationKey(SPOT_RID.toString()))
                .toCompletableFuture()
                .get()
                .ownerId());
        }
    }

    @Test
    void staleActorRenewDropsTrackedOwnershipAndRunsDeactivate() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore(Clock.fixed(NOW, ZoneOffset.UTC));
        ZLinkLocationRuntime ownerA = newRuntime(store, "owner-a");
        ZLinkLocationRuntime ownerB = newRuntime(store, "owner-b");
        AtomicInteger deactivations = new AtomicInteger();
        try (ownerA; ownerB; ZLinkLocationLifecycle lifecycleA = new ZLinkLocationLifecycle(ownerA)) {
            ownerA.start(NODE_A).toCompletableFuture().get();
            ownerB.start(NODE_B).toCompletableFuture().get();
            assertEquals(ZLinkLocationWriteStatus.STORED, lifecycleA.claimActor(
                    "chat", "actor-1", NODE_A, deactivations::incrementAndGet)
                .toCompletableFuture()
                .get());

            ownerB.writeActor(
                    new systems.zlink.framework.locations.ZLinkActorLocation(
                        "actor-1", "chat", null, NODE_B, ZLinkSpotKind.ENTRY,
                        "", null, "", 0, NOW),
                    ZLinkLocationWriteIntent.TAKEOVER)
                .toCompletableFuture()
                .get();
            CompletionException error = assertThrows(
                CompletionException.class,
                () -> lifecycleA.setActorRef(
                    "chat",
                    "actor-1",
                    new ActorRef("actor-1", 1, "mesh", NODE_A))
                    .toCompletableFuture()
                    .join());
            ZLinkFrameworkException frameworkError = (ZLinkFrameworkException) error.getCause();

            assertEquals(ZLinkFrameworkErrorKind.ACTOR_LOCATION_STALE, frameworkError.kind());
            assertFalse(lifecycleA.ownsActor("chat", "actor-1"));
            assertEquals(1, deactivations.get());
            assertEquals("owner-b", store.resolveActor(new ZLinkActorLocationKey("actor-1"))
                .toCompletableFuture()
                .get()
                .ownerId());
        }
    }

    @Test
    void stopRemovesOwnerLeaseBeforeBulkRows() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore(Clock.fixed(NOW, ZoneOffset.UTC));
        ZLinkLocationRuntime runtime = newRuntime(store, "owner-a");
        try (runtime; ZLinkLocationLifecycle lifecycle = new ZLinkLocationLifecycle(runtime)) {
            runtime.start(NODE_A).toCompletableFuture().get();
            lifecycle.claimSpot("mesh", SPOT_RID.toString(), "room", NODE_A, ZLinkSpotKind.USER, null, null)
                .toCompletableFuture()
                .get();

            runtime.stop().toCompletableFuture().get();

            assertNull(store.resolveSpot(new systems.zlink.framework.locations.ZLinkSpotLocationKey(SPOT_RID.toString()))
                .toCompletableFuture()
                .get());
            assertTrue(store.readOwnerLease("owner-a")
                .toCompletableFuture().get()
                instanceof systems.zlink.framework.locations
                    .ZLinkOwnerLeaseMissing);
        }
    }

    @Test
    void actorSessionRouteBindAndRemoveTracksGeneration() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore(Clock.fixed(NOW, ZoneOffset.UTC));
        ZLinkLocationRuntime runtime = newRuntime(store, "owner-a");
        RoutingId sessionRid = RoutingId.from("session-a");
        String routeKey = java.util.HexFormat.of().formatHex(sessionRid.toBytes());
        try (runtime; ZLinkLocationLifecycle lifecycle = new ZLinkLocationLifecycle(runtime)) {
            runtime.start(NODE_A).toCompletableFuture().get();

            lifecycle.bindActorSessionRoute(sessionRid, "actor-1", NODE_A)
                .toCompletableFuture()
                .get();

            var row = store.resolveRoute(
                    new ZLinkRouteLocationKey(ZLinkRouteKind.ACTOR_SESSION, routeKey))
                .toCompletableFuture()
                .get();
            assertEquals("actor-1", new String(row.value(), java.nio.charset.StandardCharsets.UTF_8));
            assertEquals(1, store.listRouteLocations(
                    new ZLinkRouteLocationFilter(ZLinkRouteKind.ACTOR_SESSION, NODE_A, "owner-a"),
                    ZLinkPageRequest.firstPage())
                .toCompletableFuture()
                .get()
                .items()
                .size());

            lifecycle.removeActorSessionRoute(sessionRid).toCompletableFuture().get();

            assertNull(store.resolveRoute(
                    new ZLinkRouteLocationKey(ZLinkRouteKind.ACTOR_SESSION, routeKey))
                .toCompletableFuture()
                .get());
        }
    }

    private static ZLinkLocationRuntime newRuntime(ZLinkInMemoryLocationStore store, String ownerId) {
        return new ZLinkLocationRuntime(store, ownerId, Duration.ofSeconds(30), Duration.ofSeconds(5));
    }
}
