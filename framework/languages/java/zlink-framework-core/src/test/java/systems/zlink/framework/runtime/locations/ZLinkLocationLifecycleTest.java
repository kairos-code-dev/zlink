package systems.zlink.framework.runtime.locations;

import static org.junit.jupiter.api.Assertions.*;

import java.time.Duration;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.runtime.internal.locations.ZLinkLocationWriteStatus;
import systems.zlink.framework.spots.ZLinkSpotKind;

final class ZLinkLocationLifecycleTest {
    @Test
    void localActorOwnershipTracksClaimReferenceAndRelease() {
        var store = new ZLinkInMemoryLocationStore();
        try (var runtime = new ZLinkLocationRuntime(
                 store,
                 "owner-a",
                 Duration.ofSeconds(30),
                 Duration.ofSeconds(5));
             var lifecycle = new ZLinkLocationLifecycle(runtime)) {
            RoutingId node = RoutingId.from("node-a");

            assertEquals(
                ZLinkLocationWriteStatus.STORED,
                lifecycle.claimActor(
                        "player", "actor-a", node, () -> { })
                    .toCompletableFuture().join());
            assertTrue(lifecycle.ownsActor("player", "actor-a"));
            lifecycle.setActorRef(
                    "player",
                    "actor-a",
                    new ActorRef("actor-a", 7, "game", node))
                .toCompletableFuture().join();
            lifecycle.releaseActor("player", "actor-a")
                .toCompletableFuture().join();
            assertFalse(lifecycle.ownsActor("player", "actor-a"));
        }
    }

    @Test
    void closeClearsTrackedSpotAndActorMaterializations() {
        var store = new ZLinkInMemoryLocationStore();
        var runtime = new ZLinkLocationRuntime(
            store,
            "owner-a",
            Duration.ofSeconds(30),
            Duration.ofSeconds(5));
        var lifecycle = new ZLinkLocationLifecycle(runtime);
        RoutingId node = RoutingId.from("node-a");
        lifecycle.claimSpot(
                "game",
                "room-a",
                3,
                "room",
                node,
                ZLinkSpotKind.USER,
                null,
                () -> { })
            .toCompletableFuture().join();
        lifecycle.claimActor(
                "player", "actor-a", node, () -> { })
            .toCompletableFuture().join();

        lifecycle.close();

        assertFalse(lifecycle.ownsActor("player", "actor-a"));
        runtime.close();
    }
}
